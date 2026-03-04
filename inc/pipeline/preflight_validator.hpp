#ifndef ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
#define ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP

// std
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"

namespace alchemy::pipeline::preflight_validator {

namespace detail {

// helper: check if file exists
alchemy::core::Result<std::monostate>
checkIfFileExists(const std::string& file);

// helper: check if path is a regular file
alchemy::core::Result<std::monostate>
checkIsRegularFile(const std::string& file);

// helper: check if file is writable
alchemy::core::Result<std::monostate>
checkIsFileWritable(const std::string& file);

// helper: check if parent directory is writable
alchemy::core::Result<std::monostate>
checkIsParentDirWritable(const std::string& file);

}  // namespace detail

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipe(const std::string& file);

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipes(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes);

}  // namespace alchemy::pipeline::preflight_validator
#endif  // ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
