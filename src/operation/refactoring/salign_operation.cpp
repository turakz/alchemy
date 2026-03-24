// src/operation/refactoring/salign_operation.cpp
#include "operation/refactoring/salign_operation.hpp"

// std
#include <cmath>
#include <cstddef>

#include <algorithm>
#include <iterator>
#include <numeric>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// local
#include "app/core/core.hpp"
#include "metrics/metrics.hpp"
#include "metrics/salign_metrics.hpp"
#include "operation/operation_base.hpp"
#include "parsing/artifacts/artifacts.hpp"

namespace alchemy::operation::refactoring::detail {

// build source-faithful replacement text for a reordered field
std::string
buildReplacementText(const alchemy::parser::artifacts::FieldDef& field)
{
  std::string text;

  // prepend preceding comment if present (travels with the field)
  if (!field.precedingComment.empty())
  {
    text = field.precedingComment + field.commentFieldGap;
  }

  // field declaration (use source-faithful array suffix to preserve macros)
  text += field.sourceTypeName + " " + field.fieldName +
          field.sourceArraySuffix + ";";

  if (!field.trailingComment.empty())
  {
    text += " " + field.trailingComment;
  }

  return text;
}

}  // namespace alchemy::operation::refactoring::detail

// helper: compute cache line metrics for a given struct size
alchemy::operation::refactoring::StructAlignmentOperation::CacheMetrics
alchemy::operation::refactoring::StructAlignmentOperation::computeCacheMetrics(
    std::size_t structSize) const
{
  alchemy::operation::refactoring::StructAlignmentOperation::CacheMetrics
      metrics{};
  const double Spcl = std::floor(static_cast<double>(CacheLineBytes) /
                                 static_cast<double>(structSize));
  const std::size_t BytesUsed = static_cast<std::size_t>(Spcl) * structSize;
  metrics.cacheWaste = CacheLineBytes - BytesUsed;
  metrics.cacheUtil =
      (static_cast<double>(BytesUsed) / static_cast<double>(CacheLineBytes)) *
      100.0;
  return metrics;
}

std::vector<std::size_t>
alchemy::operation::refactoring::StructAlignmentOperation::
    sortFieldsByAlignment(
        const std::vector<alchemy::parser::artifacts::FieldDef>& fields) const
{
  std::vector<std::size_t> indices(fields.size());
  std::iota(std::begin(indices), std::end(indices), std::size_t{0});
  std::stable_sort(
      std::begin(indices),
      std::end(indices),
      [&fields](std::size_t lhs, std::size_t rhs) {
        // partition: reorderable fields first (sorted by alignment
        // desc), then non-reorderable (preserve order) stable_sort
        // preserves original field order within same-alignment
        // groups, avoiding pointless churn when fields share a type
        return std::tie(fields[lhs].canReorder, fields[lhs].naturalAlignment) >
               std::tie(fields[rhs].canReorder, fields[rhs].naturalAlignment);
      });
  return indices;
}

alchemy::operation::refactoring::StructOptimization
alchemy::operation::refactoring::StructAlignmentOperation::analyzeStruct(
    const alchemy::parser::artifacts::StructDef& structDef) const
{
  // phase 1: layout, waste, cache metrics
  // ---
  alchemy::operation::refactoring::StructOptimization optimization;

  // init metrics from current (parsed) layout
  optimization.metrics.structName = structDef.structName;
  optimization.metrics.sourceFile = structDef.sourceFile;
  optimization.metrics.naturalTotalSize = structDef.naturalTotalSize;
  optimization.metrics.currentDataSize = structDef.currentDataSize;
  optimization.metrics.naturalAlignment = structDef.naturalAlignment;
  optimization.metrics.currentWastedBytes = structDef.currentWastedBytes;

  // compute waste
  optimization.metrics.currentWastePercent =
      alchemy::metrics::calculatePercentage(
          optimization.metrics.currentWastedBytes,
          optimization.metrics.naturalTotalSize);

  // compute cache metrics
  auto currentCache = alchemy::operation::refactoring::
      StructAlignmentOperation::computeCacheMetrics(structDef.naturalTotalSize);
  optimization.metrics.currentCacheWaste = currentCache.cacheWaste;
  optimization.metrics.currentCacheUtil = currentCache.cacheUtil;

  // skip #pragma pack structs — field order defines binary layout,
  // and pack(N) already eliminates or constrains padding so reordering
  // provides zero size benefit
  if (structDef.isPacked)
  {
    optimization.metrics.skipped = true;
    return optimization;
  }

  // phase 2: optimal layout
  // ---

  // compute optimized size
  auto sortedOrder = alchemy::operation::refactoring::StructAlignmentOperation::
      sortFieldsByAlignment(structDef.fields);

  const std::size_t OptimizedSize =
      alchemy::parser::artifacts::StructDef::computeSize(
          structDef.fields, sortedOrder, structDef.naturalAlignment);

  // compute optimized waste (order-independent sum)
  const std::size_t OptimizedDataSize =
      alchemy::parser::artifacts::StructDef::computeDataSize(structDef.fields);

  const std::size_t OptimizedWaste = OptimizedSize - OptimizedDataSize;

  // compute optimized metrics
  optimization.metrics.optimizedSize = OptimizedSize;
  optimization.metrics.optimizedWaste = OptimizedWaste;
  optimization.metrics.optimizedWastePercent =
      alchemy::metrics::calculatePercentage(OptimizedWaste, OptimizedSize);

  // compute optimized cache metrics
  auto optimizedCacheMetrics = alchemy::operation::refactoring::
      StructAlignmentOperation::computeCacheMetrics(OptimizedSize);
  optimization.metrics.optimizedCacheWaste = optimizedCacheMetrics.cacheWaste;
  optimization.metrics.optimizedCacheUtil = optimizedCacheMetrics.cacheUtil;

  // phase 3: check if struct would benefit from being refactored (or if it's
  // sorted already)
  // ---

  if (OptimizedSize < structDef.naturalTotalSize)
  {
    // compute savings
    optimization.metrics.possibleSavings =
        structDef.naturalTotalSize - OptimizedSize;
    optimization.metrics.savingsPercent = alchemy::metrics::calculatePercentage(
        optimization.metrics.possibleSavings, structDef.naturalTotalSize);
    // generate refactoring recipes
    for (std::size_t idx = 0; idx < sortedOrder.size(); ++idx)
    {
      const auto& originalField = structDef.fields[idx];
      const auto& optimizedField = structDef.fields[sortedOrder[idx]];

      // only generate recipes for fields that would be swapped
      if (originalField.fieldName != optimizedField.fieldName)
      {
        // skip if either field is non-reorderable
        // -> the optimized field's typeName may be
        // unrepresentable as valid C source (e.g., clang
        // emits "(unnamed union at path:line:col)" for anonymous unions)
        if (!originalField.canReorder || !optimizedField.canReorder)
        {
          // struct contains non-reorderable fields in the swap zone
          // bail out entirely to avoid corrupting the source file
          optimization.recipes.clear();
          optimization.metrics.possibleSavings = 0;
          optimization.metrics.savingsPercent = 0.0;
          break;
        }

        // create recipe
        alchemy::operation::RefactorRecipe recipe;
        recipe.sourceFile = structDef.sourceFile;
        recipe.byteOffset = originalField.byteOffset;
        recipe.byteLength = originalField.byteLength;
        recipe.replacementText =
            alchemy::operation::refactoring::detail::buildReplacementText(
                optimizedField);

        // track recipe metrics
        optimization.recipes.emplace_back(recipe);
      }
    }

    // handle recipe conflicts (merge or reject)
    if (!optimization.recipes.empty())
    {
      // sort by byteOffsets to detect overlapping ranges
      std::sort(std::begin(optimization.recipes),
                std::end(optimization.recipes),
                [](const auto& lhs, const auto& rhs) {
                  return lhs.byteOffset < rhs.byteOffset;
                });

      // check for overlaps
      for (std::size_t idx = 1; idx < optimization.recipes.size(); ++idx)
      {
        const auto& prev = optimization.recipes[idx - 1];
        const auto& curr = optimization.recipes[idx];
        const std::size_t PrevByteOffsetEnd = prev.byteOffset + prev.byteLength;

        if (curr.byteOffset < PrevByteOffsetEnd)
        {
          // exact duplicate: skip (idempotent)
          if (curr.byteOffset == prev.byteOffset &&
              curr.byteLength == prev.byteLength &&
              curr.replacementText == prev.replacementText)
          {
            continue;
          }
          // note: overlapping byte ranges indicate malformed AST
          // -> unlike clang's Replacements::add() which tests commutativity
          // for general refactoring, struct alignment recipes come from
          // non-overlapping fields
          optimization.recipes.clear();
          optimization.metrics.possibleSavings = 0;
          optimization.metrics.savingsPercent = 0.0;
          break;
        }
      }
    }
  }
  // else: no refactoring applies —> metrics default-initialized to zero

  return optimization;
}

alchemy::core::Result<alchemy::operation::RecipeOperationResult>
alchemy::operation::refactoring::StructAlignmentOperation::execute(
    const alchemy::parser::artifacts::ParseResults& artifacts) const
{
  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipesByFile;
  std::vector<alchemy::metrics::Metrics> allMetrics;

  // single pass: analyze each struct once
  for (const auto& structDef : artifacts.structs)
  {
    // computes optimal layout, generates recipes and metrics
    auto optimization = alchemy::operation::refactoring::
        StructAlignmentOperation::analyzeStruct(structDef);

    // group recipes by source file
    if (!optimization.recipes.empty())
    {
      auto& fileRecipes = recipesByFile[structDef.sourceFile];
      fileRecipes.insert(
          std::end(fileRecipes),
          std::make_move_iterator(std::begin(optimization.recipes)),
          std::make_move_iterator(std::end(optimization.recipes)));
    }

    // track metrics
    allMetrics.emplace_back(std::move(optimization.metrics));
  }

  return alchemy::core::Result<alchemy::operation::RecipeOperationResult>::
      success(
          alchemy::operation::RecipeOperationResult{getName(),  // operationName
                                                    std::move(recipesByFile),
                                                    std::move(allMetrics)});
}
