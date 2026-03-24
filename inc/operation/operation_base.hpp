// inc/operation_base.hpp
#ifndef ALCHEMY_OPERATIONS_OPERATION_BASE_HPP
#define ALCHEMY_OPERATIONS_OPERATION_BASE_HPP
// std
#include <cstddef>

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// local
#include "metrics/metrics.hpp"

namespace alchemy::operation {

struct RefactorRecipe {
  std::string sourceFile;
  std::string replacementText;
  std::size_t byteOffset{0};
  std::size_t byteLength{0};
};

using Recipe = std::variant<alchemy::operation::RefactorRecipe>;

// operations produce `detail::Recipe`s (tagged union of recipe types)
// -> result can be either recipes (for refactoring) or generated files (for
// codegen)
struct RecipeOperationResult {
  std::string operationName;
  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  // operation-specific metrics (unified variant)
  std::vector<alchemy::metrics::Metrics> metrics;
};

}  // namespace alchemy::operation
#endif  // ALCHEMY_OPERATIONS_OPERATION_BASE_HPP
