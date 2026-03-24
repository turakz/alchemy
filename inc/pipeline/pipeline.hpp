// inc/pipeline.hpp
#ifndef ALCHEMY_PIPELINE_PIPELINE_HPP
#define ALCHEMY_PIPELINE_PIPELINE_HPP

// std
#include <cstddef>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"
#include "metrics/metrics.hpp"
#include "operation/operation_base.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/parser.hpp"
#include "parsing/parsing_requirements.hpp"

namespace alchemy::pipeline {

namespace detail {

// helper: gather parsing requirements from all operation variants
template <typename RecipeOperationVariantT>
alchemy::parser::ParsingRequirements
gatherRequirements(const std::vector<RecipeOperationVariantT>& operations)
{
  alchemy::parser::ParsingRequirements combinedParseReqs{};
  for (const auto& operation : operations)
  {
    std::visit(
        [&combinedParseReqs](const auto& operationImpl) {
          combinedParseReqs |= operationImpl.getRequirements();
        },
        operation);
  }
  return combinedParseReqs;
}

}  // namespace detail

// stage 1: parse source files based on operation requirements
template <typename RecipeOperationVariantT>
alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
runParser(alchemy::parser::ParsingRuleAdapter& parser,
          const std::vector<RecipeOperationVariantT>& operations)
{
  const alchemy::parser::ParsingRequirements Requirements =
      detail::gatherRequirements(operations);
  alchemy::core::Result<alchemy::parser::artifacts::ParseResults> parseResults =
      parser.parse(Requirements);

  if (parseResults.invalid())
  {
    return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
        failure(alchemy::core::Error::format("alchemy::parser::artifacts",
                                             "parsing failed: {}",
                                             parseResults.error()));
  }

  return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
      success(std::move(parseResults).value());
}

// stage 2: execute all operations on parsed artifacts
template <typename RecipeOperationVariantT>
std::vector<alchemy::core::Result<alchemy::operation::RecipeOperationResult>>
executeOperations(const alchemy::parser::artifacts::ParseResults& artifacts,
                  const std::vector<RecipeOperationVariantT>& operations)
{
  std::vector<alchemy::core::Result<alchemy::operation::RecipeOperationResult>>
      results;
  results.reserve(operations.size());

  std::transform(std::begin(operations),
                 std::end(operations),
                 std::back_inserter(results),
                 // operations are functors
                 [&artifacts](const RecipeOperationVariantT& operation) {
                   return std::visit(
                       [&artifacts](const auto& recipeOperationImpl)
                           -> alchemy::core::Result<
                               alchemy::operation::RecipeOperationResult> {
                         return recipeOperationImpl.execute(artifacts);
                       },
                       operation);
                 });

  return results;
}

// stage 3: transmute all recipes (apply file changes)
// aggregate summary of transmutations across all files
struct TransmutationSummary {
  std::size_t recipesApplied{0};
  std::size_t filesProcessed{0};
};

alchemy::core::Result<bool>
validateTransmute(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes);

alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
executeTransmute(const std::unordered_map<
                     std::string,
                     std::vector<alchemy::operation::Recipe>>& allRecipes,
                 bool dryRun);

alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
transmute(const std::unordered_map<std::string,
                                   std::vector<alchemy::operation::Recipe>>&
              allRecipes,
          bool dryRun);

// full pipeline execution result
struct PipelineResult {
  std::vector<alchemy::metrics::Metrics> allMetrics;
  TransmutationSummary summary;
};

// full pipeline: parse → execute → transmute → report
template <typename RecipeOperationVariantT>
alchemy::core::Result<PipelineResult>
execute(alchemy::parser::ParsingRuleAdapter& parser,
        const std::vector<RecipeOperationVariantT>& operations,
        bool dryRun)
{
  alchemy::pipeline::PipelineResult pipelineResult;
  pipelineResult.summary.recipesApplied = 0;
  pipelineResult.summary.filesProcessed = 0;

  // stage 1: parse
  alchemy::core::Result<alchemy::parser::artifacts::ParseResults> artifacts =
      alchemy::pipeline::runParser(parser, operations);
  if (artifacts.invalid())
  {
    return alchemy::core::Result<alchemy::pipeline::PipelineResult>::failure(
        alchemy::core::Error::format("alchemy::pipeline",
                                     "artifacts not parsed: {}",
                                     artifacts.error()));
  }

  // stage 2: execute recipe operations
  std::vector<alchemy::core::Result<alchemy::operation::RecipeOperationResult>>
      operationResults =
          alchemy::pipeline::executeOperations<RecipeOperationVariantT>(
              artifacts.value(), operations);

  // stage 3: apply operations and collect metrics
  for (auto& result : operationResults)
  {
    if (!result.valid())
    {
      return alchemy::core::Result<alchemy::pipeline::PipelineResult>::failure(
          alchemy::core::Error::format("alchemy::pipeline",
                                       "recipe operation failed: {}",
                                       result.error()));
    }

    // transmute recipes
    alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
        transmutationSummary =
            alchemy::pipeline::transmute(result.value().recipes, dryRun);

    if (!transmutationSummary.valid())
    {
      return alchemy::core::Result<alchemy::pipeline::PipelineResult>::failure(
          alchemy::core::Error::format("alchemy::pipeline",
                                       "recipe transmutation failed: {}",
                                       transmutationSummary.error()));
    }

    // both valid: accumulate metrics and summary
    pipelineResult.allMetrics.insert(std::end(pipelineResult.allMetrics),
                                     std::begin(result.value().metrics),
                                     std::end(result.value().metrics));

    pipelineResult.summary.recipesApplied +=
        transmutationSummary.value().recipesApplied;
    pipelineResult.summary.filesProcessed +=
        transmutationSummary.value().filesProcessed;
  }

  return alchemy::core::Result<alchemy::pipeline::PipelineResult>::success(
      std::move(pipelineResult));
}

}  // namespace alchemy::pipeline
#endif  // ALCHEMY_PIPELINE_PIPELINE_HPP
