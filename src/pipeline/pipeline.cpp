// src/pipeline.cpp
#include "pipeline/pipeline.hpp"

// std
#include <cstddef>

#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "logger/logger.hpp"
#include "operation/operation_base.hpp"
#include "pipeline/preflight_validator.hpp"
#include "transmute/transmute.hpp"

// helper: execute transmutation (apply recipes to files)
alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
alchemy::pipeline::executeTransmute(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes,
    bool dryRun)
{
  alchemy::pipeline::TransmutationSummary result;

  alchemy::logger::info("alchemy::{}transmuting{}...\n",
                        alchemy::color::ansi::BoldBrightGreen,
                        alchemy::color::ansi::Reset);

  std::size_t totalRecipes =
      std::accumulate(std::begin(allRecipes),
                      std::end(allRecipes),
                      static_cast<std::size_t>(0),
                      [](std::size_t sum, const auto& pair) {
                        return sum + pair.second.size();
                      });

  alchemy::logger::info(
      "alchemy::{}pipeline{}::{} recipes across {} files ready for "
      "{}transmutation{}...\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      totalRecipes,
      allRecipes.size(),
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset);

  for (const auto& [file, recipes] : allRecipes)
  {
    alchemy::core::Result<alchemy::transmute::TransmutationResult>
        transmuteResult =
            alchemy::transmute::applyRecipes(file, recipes, dryRun);

    if (transmuteResult.valid())
    {
      result.recipesApplied += transmuteResult.value().recipesApplied;
      result.filesProcessed += 1;
    }
    else
    {
      return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
          failure(alchemy::core::Error::format(
              "alchemy::pipeline::executeTransmute",
              "transmutation failed for {}: {}",
              file,
              std::move(transmuteResult).error()));
    }
  }

  return alchemy::core::Result<
      alchemy::pipeline::TransmutationSummary>::success(result);
}

// helper: validate recipes before transmutation
alchemy::core::Result<bool>
alchemy::pipeline::validateTransmute(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes)
{
  if (allRecipes.empty())
  {
    alchemy::logger::info("alchemy::{}pipeline{}::{}no recipes generated{}\n",
                          alchemy::color::ansi::BoldBrightGreen,
                          alchemy::color::ansi::Reset,
                          alchemy::color::ansi::Magenta,
                          alchemy::color::ansi::Reset);
    return alchemy::core::Result<bool>::success(true);
  }

  alchemy::logger::debug(
      "alchemy::pipeline::validateTransmute: validating {} files\n",
      allRecipes.size());

  // pre-flight validation: we either can transmute all files, or don't bother
  // (avoiding partially mutated/generated state)
  auto validationResult =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipes(
          allRecipes);
  if (validationResult.invalid())
  {
    return alchemy::core::Result<bool>::failure(
        std::move(validationResult).error());
  }

  return alchemy::core::Result<bool>::success(true);
}

// stage 3: transmute all recipes (apply file changes)
alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
alchemy::pipeline::transmute(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes,
    bool dryRun)
{
  auto validation = alchemy::pipeline::validateTransmute(allRecipes);
  if (validation.invalid())
  {
    return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
        failure(std::move(validation).error());
  }

  // execute transmutation
  auto result = alchemy::pipeline::executeTransmute(allRecipes, dryRun);
  if (result.invalid())
  {
    return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
        failure(std::move(result).error());
  }

  return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
      success(std::move(result).value());
}
