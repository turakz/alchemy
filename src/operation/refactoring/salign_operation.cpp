// src/operation/refactoring/salign_operation.cpp
#include "operation/refactoring/salign_operation.hpp"

// std
#include <cmath>
#include <cstddef>

#include <algorithm>
#include <iterator>
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

std::vector<alchemy::parser::artifacts::FieldDef>
alchemy::operation::refactoring::StructAlignmentOperation::
    sortFieldsByAlignment(
        const std::vector<alchemy::parser::artifacts::FieldDef>& fields) const
{
  std::vector<alchemy::parser::artifacts::FieldDef> sorted = fields;
  std::stable_sort(std::begin(sorted),
                   std::end(sorted),
                   [](const alchemy::parser::artifacts::FieldDef& lhs,
                      const alchemy::parser::artifacts::FieldDef& rhs) {
                     // partition: reorderable fields first (sorted by alignment
                     // desc), then non-reorderable (preserve order) stable_sort
                     // preserves original field order within same-alignment
                     // groups, avoiding pointless churn when fields share a
                     // type
                     return std::tie(lhs.canReorder, lhs.naturalAlignment) >
                            std::tie(rhs.canReorder, rhs.naturalAlignment);
                   });
  return sorted;
}

alchemy::operation::refactoring::StructAnalysis
alchemy::operation::refactoring::StructAlignmentOperation::analyzeStruct(
    const alchemy::parser::artifacts::StructDef& structDef) const
{
  // phase 1: layout, waste, cache metrics
  // ---
  alchemy::operation::refactoring::StructAnalysis analyzedStruct;

  // init metrics from current (parsed) layout
  analyzedStruct.metrics.structName = structDef.structName;
  analyzedStruct.metrics.sourceFile = structDef.sourceFile;
  analyzedStruct.metrics.naturalTotalSize = structDef.naturalTotalSize;
  analyzedStruct.metrics.currentDataSize = structDef.currentDataSize;
  analyzedStruct.metrics.naturalAlignment = structDef.naturalAlignment;
  analyzedStruct.metrics.currentWastedBytes = structDef.currentWastedBytes;

  // compute waste
  analyzedStruct.metrics.currentWastePercent =
      alchemy::metrics::detail::calculatePercentage(
          analyzedStruct.metrics.currentWastedBytes,
          analyzedStruct.metrics.naturalTotalSize);

  // compute cache metrics
  auto currentCache = alchemy::operation::refactoring::
      StructAlignmentOperation::computeCacheMetrics(structDef.naturalTotalSize);
  analyzedStruct.metrics.currentCacheWaste = currentCache.cacheWaste;
  analyzedStruct.metrics.currentCacheUtil = currentCache.cacheUtil;

  // skip #pragma pack structs — field order defines binary layout,
  // and pack(N) already eliminates or constrains padding so reordering
  // provides zero size benefit
  if (structDef.isPacked)
  {
    analyzedStruct.metrics.skipped = true;
    return analyzedStruct;
  }

  // phase 2: optimal layout
  // ---

  // compute optimized size
  auto optimizedFields = alchemy::operation::refactoring::
      StructAlignmentOperation::sortFieldsByAlignment(structDef.fields);

  const std::size_t OptimizedSize =
      alchemy::parser::artifacts::StructDef::computeSize(
          optimizedFields, structDef.naturalAlignment);

  // compute optimized waste
  const std::size_t OptimizedDataSize =
      alchemy::parser::artifacts::StructDef::computeDataSize(optimizedFields);

  const std::size_t OptimizedWaste = OptimizedSize - OptimizedDataSize;

  // compute optimized metrics
  analyzedStruct.metrics.optimizedSize = OptimizedSize;
  analyzedStruct.metrics.optimizedWaste = OptimizedWaste;
  analyzedStruct.metrics.optimizedWastePercent =
      alchemy::metrics::detail::calculatePercentage(OptimizedWaste,
                                                    OptimizedSize);

  // compute optimized cache metrics
  auto optimizedCacheMetrics = alchemy::operation::refactoring::
      StructAlignmentOperation::computeCacheMetrics(OptimizedSize);
  analyzedStruct.metrics.optimizedCacheWaste = optimizedCacheMetrics.cacheWaste;
  analyzedStruct.metrics.optimizedCacheUtil = optimizedCacheMetrics.cacheUtil;

  // phase 3: check if struct would benefit from being refactored (or if it's
  // sorted already)
  // ---

  if (OptimizedSize < structDef.naturalTotalSize)
  {
    // compute savings
    analyzedStruct.metrics.possibleSavings =
        structDef.naturalTotalSize - OptimizedSize;
    analyzedStruct.metrics.savingsPercent =
        alchemy::metrics::detail::calculatePercentage(
            analyzedStruct.metrics.possibleSavings, structDef.naturalTotalSize);
    // generate refactoring recipes
    for (std::size_t idx = 0; idx < optimizedFields.size(); ++idx)
    {
      const auto& originalField = structDef.fields[idx];
      const auto& optimizedField = optimizedFields[idx];

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
          analyzedStruct.recipes.clear();
          analyzedStruct.metrics.possibleSavings = 0;
          analyzedStruct.metrics.savingsPercent = 0.0;
          break;
        }

        // create recipe
        alchemy::operation::detail::RefactorRecipe recipe;
        recipe.sourceFile = structDef.sourceFile;
        recipe.byteOffset = originalField.byteOffset;
        recipe.byteLength = originalField.byteLength;
        recipe.replacementText =
            alchemy::operation::refactoring::detail::buildReplacementText(
                optimizedField);

        // track recipe metrics
        analyzedStruct.recipes.emplace_back(recipe);
      }
    }

    // handle recipe conflicts (merge or reject)
    if (!analyzedStruct.recipes.empty())
    {
      // sort by byteOffsets to detect overlapping ranges
      std::sort(std::begin(analyzedStruct.recipes),
                std::end(analyzedStruct.recipes),
                [](const auto& lhs, const auto& rhs) {
                  return lhs.byteOffset < rhs.byteOffset;
                });

      // check for overlaps
      for (std::size_t idx = 1; idx < analyzedStruct.recipes.size(); ++idx)
      {
        const auto& prev = analyzedStruct.recipes[idx - 1];
        const auto& curr = analyzedStruct.recipes[idx];
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
          analyzedStruct.recipes.clear();
          analyzedStruct.metrics.possibleSavings = 0;
          analyzedStruct.metrics.savingsPercent = 0.0;
          break;
        }
      }
    }
  }
  // else: no refactoring applies —> metrics default-initialized to zero

  return analyzedStruct;
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
    auto analysis = alchemy::operation::refactoring::StructAlignmentOperation::
        analyzeStruct(structDef);

    // group recipes by source file
    if (!analysis.recipes.empty())
    {
      auto& fileRecipes = recipesByFile[structDef.sourceFile];
      fileRecipes.insert(std::end(fileRecipes),
                         std::make_move_iterator(std::begin(analysis.recipes)),
                         std::make_move_iterator(std::end(analysis.recipes)));
    }

    // track metrics
    allMetrics.emplace_back(std::move(analysis.metrics));
  }

  return alchemy::core::Result<alchemy::operation::RecipeOperationResult>::
      success(
          alchemy::operation::RecipeOperationResult{getName(),  // operationName
                                                    std::move(recipesByFile),
                                                    std::move(allMetrics)});
}
