// src/pipeline.cpp
// std
#include <cstdlib>

#include <filesystem>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

// 3rd party
#include "fmt/core.h"

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/preflight_validator.hpp"
#include "transmute/transmute.hpp"

// helper: execute transmutation (apply recipes to files)
alchemy::core::Result<alchemy::pipeline::TransmutationSummary>
alchemy::pipeline::executeTransmute(
    const std::unordered_map<std::filesystem::path,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes,
    const std::filesystem::path& buildDir,
    bool dryRun)
{
  alchemy::pipeline::TransmutationSummary result;

  std::size_t totalRecipes =
      std::accumulate(allRecipes.begin(),
                      allRecipes.end(),
                      static_cast<std::size_t>(0),
                      [](std::size_t sum, const auto& pair) {
                        return sum + pair.second.size();
                      });

  fmt::print("alchemy::{}pipeline{}::{} recipes across {} files ready for "
             "{}transmutation{}...\n",
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset,
             totalRecipes,
             allRecipes.size(),
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);

  for (const auto& [file, recipes] : allRecipes)
  {
    alchemy::core::Result<alchemy::transmute::TransmutationResult>
        transmuteResult =
            alchemy::transmute::applyRecipes(file, recipes, buildDir, dryRun);

    if (transmuteResult.valid())
    {
      fmt::print("alchemy::{}pipeline{}::{}{}{} -- {} recipes applied\n",
                 alchemy::color::ansi::BrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BrightGreen,
                 transmuteResult.value().file.string(),
                 alchemy::color::ansi::Reset,
                 transmuteResult.value().recipesApplied);

      result.recipesApplied += transmuteResult.value().recipesApplied;
      result.filesProcessed += 1;
    }
    else
    {
      return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
          failure(alchemy::core::Error::format(
              "alchemy::pipeline::executeTransmute",
              "transmutation failed for {}: {}",
              file.string(),
              std::move(transmuteResult).error()));
    }
  }

  return alchemy::core::Result<
      alchemy::pipeline::TransmutationSummary>::success(result);
}

// helper: validate recipes before transmutation
alchemy::core::Result<bool>
alchemy::pipeline::validateTransmute(
    const std::unordered_map<std::filesystem::path,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes)
{
  if (allRecipes.empty())
  {
    fmt::print("alchemy::{}pipeline{}::{}no recipes generated{}\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::Magenta,
               alchemy::color::ansi::Reset);
    return alchemy::core::Result<bool>::success(true);
  }

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
    const std::unordered_map<std::filesystem::path,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes,
    const std::filesystem::path& buildDir,
    bool dryRun)
{
  auto validation = alchemy::pipeline::validateTransmute(allRecipes);
  if (validation.invalid())
  {
    return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
        failure(std::move(validation).error());
  }

  // if empty, return success early
  if (allRecipes.empty())
  {
    return alchemy::core::Result<
        alchemy::pipeline::TransmutationSummary>::success({});
  }

  // execute transmutation
  auto result =
      alchemy::pipeline::executeTransmute(allRecipes, buildDir, dryRun);
  if (result.invalid())
  {
    return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
        failure(std::move(result).error());
  }

  return alchemy::core::Result<alchemy::pipeline::TransmutationSummary>::
      success(std::move(result).value());
}
