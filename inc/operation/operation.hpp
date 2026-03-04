#ifndef ALCHEMY_OPERATION_OPERATION_HPP
#define ALCHEMY_OPERATION_OPERATION_HPP
// std
#include <variant>

// 3rd party

// local
#include "operation/refactoring/salign_operation.hpp"

namespace alchemy::operation {
using RecipeOperation =
    std::variant<alchemy::operation::refactoring::StructAlignmentOperation>;

}  // namespace alchemy::operation
#endif  // ALCHEMY_OPERATION_OPERATION_HPP
