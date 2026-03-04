#ifndef ALCHEMY_TRANSMUTE_TRANSMUTE_HPP
#define ALCHEMY_TRANSMUTE_TRANSMUTE_HPP

// std
#include <cstddef>

#include <string>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"

namespace alchemy::transmute {

struct TransmutationResult {
  std::string file;
  std::size_t recipesApplied{0};
};

alchemy::core::Result<alchemy::transmute::TransmutationResult>
applyRefactor(
    const std::string& sourceFile,
    const std::vector<alchemy::operation::detail::RefactorRecipe>& recipes,
    bool dryRun);

alchemy::core::Result<alchemy::transmute::TransmutationResult>
applyRecipes(const std::string& sourceFile,
             const std::vector<alchemy::operation::Recipe>& recipes,
             bool dryRun);

}  // namespace alchemy::transmute
#endif  // ALCHEMY_TRANSMUTE_TRANSMUTE_HPP
