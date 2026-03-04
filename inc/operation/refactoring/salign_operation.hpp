#ifndef ALCHEMY_OPERATION_REFACTORING_SALIGN_OPERATION_HPP
#define ALCHEMY_OPERATION_REFACTORING_SALIGN_OPERATION_HPP
// std
#include <cstddef>

#include <string>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"
#include "metrics/salign_metrics.hpp"
#include "operation/operation_base.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/parsing_requirements.hpp"

namespace alchemy::operation::refactoring {

namespace detail {

// build source-faithful replacement text for a reordered field
std::string
buildReplacementText(const alchemy::parser::artifacts::FieldDef& field);

}  // namespace detail

struct StructAnalysis {
  std::vector<alchemy::operation::detail::RefactorRecipe> recipes;
  alchemy::metrics::detail::SAlignMetrics metrics;
};

class StructAlignmentOperation {
public:
  // typical cache line size for struct packing analysis
  static constexpr std::size_t CacheLineBytes = 64;

  // cache metrics result
  struct CacheMetrics {
    std::size_t cacheWaste;
    double cacheUtil;
  };

  CacheMetrics
  computeCacheMetrics(std::size_t structSize) const;

  // sort fields for optimal alignment: reorderable first (by alignment desc),
  // then non-reorderable
  std::vector<alchemy::parser::artifacts::FieldDef>
  sortFieldsByAlignment(
      const std::vector<alchemy::parser::artifacts::FieldDef>& fields) const;

  StructAnalysis
  analyzeStruct(const alchemy::parser::artifacts::StructDef& structDef) const;

  alchemy::parser::ParsingRequirements
  getRequirements() const
  {
    alchemy::parser::ParsingRequirements reqs;
    reqs.needsStructParsing = true;
    return reqs;
  }

  std::string
  getName() const
  {
    return std::string{"StructAlignment"};
  }

  alchemy::core::Result<alchemy::operation::RecipeOperationResult>
  execute(const alchemy::parser::artifacts::ParseResults& artifacts) const;
};
}  // namespace alchemy::operation::refactoring
#endif  // ALCHEMY_OPERATION_REFACTORING_SALIGN_OPERATION_HPP
