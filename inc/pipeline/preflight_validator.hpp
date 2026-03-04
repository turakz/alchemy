#ifndef ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
#define ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP

// std
#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// 3rd party
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"

// local

namespace alchemy::pipeline::preflight_validator {

struct ValidatorError {
  std::filesystem::path file;
  std::string msg;
};

namespace detail {

// helper: check if file exists
alchemy::core::Result<std::monostate>
checkIfFileExists(const std::filesystem::path& file);

// helper: check if path is a regular file
alchemy::core::Result<std::monostate>
checkIsRegularFile(const std::filesystem::path& file);

// helper: check if file is writable
alchemy::core::Result<std::monostate>
checkIsFileWritable(const std::filesystem::path& file);

// helper: check if parent directory is writable
alchemy::core::Result<std::monostate>
checkIsParentDirWritable(const std::filesystem::path& file);

}  // namespace detail

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipe(const std::filesystem::path& file);

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipes(
    const std::unordered_map<std::filesystem::path,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes);

}  // namespace alchemy::pipeline::preflight_validator
#endif  // ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
