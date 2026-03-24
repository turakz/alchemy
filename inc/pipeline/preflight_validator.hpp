#ifndef ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
#define ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP

// std
#include <string>
#include <string_view>
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
checkIfFileExists(std::string_view file);

// helper: check if path is a regular file
alchemy::core::Result<std::monostate>
checkIsRegularFile(std::string_view file);

// helper: check if file is writable
alchemy::core::Result<std::monostate>
checkIsFileWritable(std::string_view file);

// helper: check if parent directory is writable
alchemy::core::Result<std::monostate>
checkIsParentDirWritable(std::string_view file);

}  // namespace detail

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipe(std::string_view file);

alchemy::core::Result<std::monostate>
checkIfCanApplyRecipes(
    const std::unordered_map<std::string,
                             std::vector<alchemy::operation::Recipe>>&
        allRecipes);

}  // namespace alchemy::pipeline::preflight_validator
#endif  // ALCHEMY_PIPELINE_PREFLIGHT_VALIDATOR_HPP
