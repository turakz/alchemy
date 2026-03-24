// src/pipeline/preflight_validator.cpp
// std
#include <filesystem>
#include <unordered_map>
#include <variant>
#include <vector>

// 3rd party
#include "fmt/core.h"

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"
#include "pipeline/preflight_validator.hpp"

// helper: check if file exists
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIfFileExists(
    const std::filesystem::path& file)
{
  if (!std::filesystem::exists(file))
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format(
            "alchemy::transmute", "file does not exist: {}", file.string()));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if path is a regular file
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsRegularFile(
    const std::filesystem::path& file)
{
  if (!std::filesystem::is_regular_file(file))
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format(
            "alchemy::transmute", "not a regular file: {}", file.string()));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if file is writable
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsFileWritable(
    const std::filesystem::path& file)
{
  auto perms = std::filesystem::status(file).permissions();
  if ((perms & std::filesystem::perms::owner_write) ==
      std::filesystem::perms::none)
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format(
            "alchemy::transmute", "file is not writable: {}", file.string()));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if parent directory is writable
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsParentDirWritable(
    const std::filesystem::path& file)
{
  auto parentPerms = std::filesystem::status(file.parent_path()).permissions();
  if ((parentPerms & std::filesystem::perms::owner_write) ==
      std::filesystem::perms::none)
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format(
            "alchemy::pipeline::preflight_validator::checkIsParentDirWritable",
            "directory is not writable: {}",
            file.parent_path().string()));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(
    const std::filesystem::path& file)
{
  if (auto result = detail::checkIfFileExists(file); result.invalid())
  {
    return result;
  }

  if (auto result = detail::checkIsRegularFile(file); result.invalid())
  {
    return result;
  }

  if (auto result = detail::checkIsFileWritable(file); result.invalid())
  {
    return result;
  }

  if (auto result = detail::checkIsParentDirWritable(file); result.invalid())
  {
    return result;
  }

  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::checkIfCanApplyRecipes(
    const std::unordered_map<std::filesystem::path,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes)
{
  std::vector<std::string> errors;
  for (const auto& [file, recipes] : allRecipes)
  {
    if (auto result =
            alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(file);
        result.invalid())
    {
      errors.push_back(result.error());
    }
  }
  if (!errors.empty())
  {
    std::string combined;
    for (const auto& err : errors)
    {
      combined += err;
    }
    return alchemy::core::Result<std::monostate>::failure(combined);
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}
