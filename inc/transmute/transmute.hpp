#ifndef ALCHEMY_TRANSMUTE_TRANSMUTE_HPP
#define ALCHEMY_TRANSMUTE_TRANSMUTE_HPP

// std
#include <cstddef>

#include <filesystem>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"

namespace alchemy::transmute {

struct TransmutationResult {
  std::filesystem::path file{};
  std::size_t recipesApplied{0};
};

alchemy::core::Result<alchemy::transmute::TransmutationResult>
applyRefactor(
    const std::filesystem::path& sourceFile,
    const std::vector<alchemy::operation::detail::RefactorRecipe>& recipes,
    const std::filesystem::path& buildDir,
    bool dryRun);

alchemy::core::Result<alchemy::transmute::TransmutationResult>
applyRecipes(const std::filesystem::path& sourceFile,
             const std::vector<alchemy::operation::Recipe>& recipes,
             const std::filesystem::path& buildDir,
             bool dryRun);

}  // namespace alchemy::transmute
#endif  // ALCHEMY_TRANSMUTE_TRANSMUTE_HPP
