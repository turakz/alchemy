// src/transmute.cpp
#include "transmute/transmute.hpp"

// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

// 3rd party
#include <fmt/core.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "app/core/utils/utils.hpp"
#include "operation/operation_base.hpp"

alchemy::core::Result<alchemy::transmute::TransmutationResult>
alchemy::transmute::applyRefactor(
    const std::string& sourceFile,
    const std::vector<alchemy::operation::detail::RefactorRecipe>& recipes,
    bool dryRun)
{
  alchemy::transmute::TransmutationResult result{.file = sourceFile,
                                                 .recipesApplied = 0};

  // read file once: as bytes
  std::ifstream file(sourceFile, std::ios::binary | std::ios::ate);
  if (!file)
  {
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(
            alchemy::core::Error::format("alchemy::transmute::applyRefactor",
                                         "failed to open {} for reading",
                                         sourceFile));
  }

  // pre-alloc
  const std::streamsize Size = file.tellg();
  std::string content(static_cast<size_t>(Size), '\0');
  file.seekg(0);
  file.read(content.data(), Size);

  if (!file)
  {
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(
            alchemy::core::Error::format("alchemy::transmute::applyRefactor",
                                         "failed to read entire file: {}",
                                         sourceFile));
  }

  const size_t OriginalSize = content.size();

  // validate all recipes: check if any recipe is out of bounds
  auto outOfBoundsRecipe = std::find_if(
      std::begin(recipes),
      std::end(recipes),
      [OriginalSize](const auto& recipe) {
        return recipe.byteOffset + recipe.byteLength > OriginalSize;
      });

  if (outOfBoundsRecipe != std::end(recipes))
  {
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(alchemy::core::Error::format(
            "alchemy::transmute::applyRefactor",
            "byte range [{}, {}) out of bounds "
            "for {} (size: {})",
            outOfBoundsRecipe->byteOffset,
            outOfBoundsRecipe->byteOffset + outOfBoundsRecipe->byteLength,
            outOfBoundsRecipe->sourceFile,
            OriginalSize));
  }

  // calculate final size
  const size_t FinalSize =
      std::accumulate(std::begin(recipes),
                      std::end(recipes),
                      OriginalSize,
                      [](size_t acc, const auto& r) {
                        return acc + r.replacementText.size() - r.byteLength;
                      });

  // build refactored content in single pass (also with pre-alloc)
  std::string newContent;
  newContent.reserve(FinalSize);

  size_t lastPos = 0;
  for (const auto& recipe : recipes)
  {
    newContent.append(content, lastPos, recipe.byteOffset - lastPos);
    newContent.append(recipe.replacementText);
    lastPos = recipe.byteOffset + recipe.byteLength;
    ++result.recipesApplied;
  }
  newContent.append(content, lastPos, content.size() - lastPos);

  // re-direct to stdout
  if (dryRun)
  {
    fmt::print("alchemy::{}transmute{}::{}dry-run{}::{}{}{}\n{}\n",
               alchemy::color::ansi::BoldBrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::Cyan,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::BrightGreen,
               sourceFile,
               alchemy::color::ansi::Reset,
               newContent);
    return alchemy::core::Result<
        alchemy::transmute::TransmutationResult>::success(std::move(result));
  }

  // write once
  const std::filesystem::path SourcePath(sourceFile);
  const std::filesystem::path TmpSource =
      SourcePath.parent_path() / (SourcePath.filename().string() + ".alch");
  std::ofstream out(TmpSource, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(
            alchemy::core::Error::format("alchemy::transmute::applyRefactor",
                                         "RefactorRecipe:"
                                         " failed to open temporary file {} "
                                         "for writing",
                                         TmpSource.string()));
  }
  out.write(newContent.data(), static_cast<std::streamsize>(newContent.size()));
  out.close();
  if (!out)
  {
    std::filesystem::remove(TmpSource);
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(
            alchemy::core::Error::format("alchemy::transmute::applyRefactor",
                                         "RefactorRecipe: "
                                         " failed to write temporary file {}",
                                         TmpSource.string()));
  }
  try
  {
    std::filesystem::rename(TmpSource, sourceFile);
  }
  catch (const std::filesystem::filesystem_error& err)
  {
    return alchemy::core::Result<alchemy::transmute::TransmutationResult>::
        failure(
            alchemy::core::Error::format("alchemy::transmute::applyRefactor",
                                         "RefactorRecipe: "
                                         " failed to rename {} to {}: {}",
                                         TmpSource.string(),
                                         sourceFile,
                                         err.what()));
  }
  return alchemy::core::Result<
      alchemy::transmute::TransmutationResult>::success(std::move(result));
}

alchemy::core::Result<alchemy::transmute::TransmutationResult>
alchemy::transmute::applyRecipes(
    const std::string& sourceFile,
    const std::vector<alchemy::operation::Recipe>& recipes,
    bool dryRun)
{
  auto refactorRecipes = alchemy::core::utils::extractVariantFrom<
      alchemy::operation::detail::RefactorRecipe>(recipes);

  alchemy::transmute::TransmutationResult result{sourceFile, 0};

  if (!refactorRecipes.empty())
  {
    auto refactorResult =
        alchemy::transmute::applyRefactor(sourceFile, refactorRecipes, dryRun);
    if (refactorResult.invalid())
    {
      return refactorResult;
    }
    result.recipesApplied += refactorResult.value().recipesApplied;
  }

  return alchemy::core::Result<
      alchemy::transmute::TransmutationResult>::success(std::move(result));
}
