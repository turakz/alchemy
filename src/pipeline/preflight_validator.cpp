// src/pipeline/preflight_validator.cpp
#include "pipeline/preflight_validator.hpp"

// std
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// 3rd party
#include <fmt/core.h>

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"

// helper: check if file exists
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIfFileExists(
    std::string_view file)
{
  if (!std::filesystem::exists(file))
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format("alchemy::pipeline::preflight_validator",
                                     "file does not exist: {}",
                                     file));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if path is a regular file
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsRegularFile(
    std::string_view file)
{
  if (!std::filesystem::is_regular_file(file))
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format("alchemy::pipeline::preflight_validator",
                                     "not a regular file: {}",
                                     file));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if file is writable
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsFileWritable(
    std::string_view file)
{
  auto perms = std::filesystem::status(file).permissions();
  if ((perms & std::filesystem::perms::owner_write) ==
      std::filesystem::perms::none)
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format("alchemy::pipeline::preflight_validator",
                                     "file is not writable: {}",
                                     file));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

// helper: check if parent directory is writable
alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::detail::checkIsParentDirWritable(
    std::string_view file)
{
  auto parentPerms =
      std::filesystem::status(std::filesystem::path(file).parent_path())
          .permissions();
  if ((parentPerms & std::filesystem::perms::owner_write) ==
      std::filesystem::perms::none)
  {
    return alchemy::core::Result<std::monostate>::failure(
        alchemy::core::Error::format(
            "alchemy::pipeline::preflight_validator::checkIsParentDirWritable",
            "directory is not writable: {}",
            std::filesystem::path(file).parent_path().string()));
  }
  return alchemy::core::Result<std::monostate>::success(std::monostate{});
}

alchemy::core::Result<std::monostate>
alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(
    std::string_view file)
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
    const std::unordered_map<std::string,
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
