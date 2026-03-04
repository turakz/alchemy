#ifndef ALCHEMY_METRICS_SALIGN_METRICS_HPP
#define ALCHEMY_METRICS_SALIGN_METRICS_HPP

// std
#include <cstddef>

#include <filesystem>
#include <string>

// 3rd party

// local

namespace alchemy::metrics::detail {

// note: some fields are stored for reporting convenience though they could
// be computed on-the-fly (e.g., currentWastedBytes = naturalTotalSize -
// currentDataSize). keeping them explicit avoids recomputation in reporters
// and makes the struct self-documenting for output formatting.
struct SAlignMetrics {
  std::filesystem::path sourceFile{};  // for reporting
  std::string structName;              // for reporting

  // current layout (from parse time)
  std::size_t naturalTotalSize{0};    // includes all padding
  std::size_t currentDataSize{0};     // sum of field sizes
  std::size_t naturalAlignment{0};    // max field alignment
  std::size_t currentWastedBytes{0};  // total padding
  double currentWastePercent{0.0};    // (currentWastedBytes / naturalTotalSize)
  double currentSpclScore{0.0};       // floor(64 / naturalTotalSize)
  double currentCacheUtil{0.0};       // (splc * size / 64) * 100
  std::size_t currentCacheWaste{0};   // bytes wasted per cache line

  // optimized layout (computed)
  std::size_t optimizedSize{0};        // predicted size after reordering
  std::size_t optimizedWaste{0};       // predicted padding after reordering
  double optimizedWastePercent{0.0};   // (optimizedWaste / optimizedSize)
  std::size_t possibleSavings{0};      // bytes saved
  double savingsPercent{0.0};          // (possibleSavings / naturalTotalSize)
  double optimizedSpclScore{0.0};      // floor(64 / optimizedSize)
  double optimizedCacheUtil{0.0};      // (splc * size / 64) * 100
  std::size_t optimizedCacheWaste{0};  // bytes wasted per cache line
  std::size_t optimizedCacheSize{0};   // lcm(optimizedSize, 64)
};

}  // namespace alchemy::metrics::detail
#endif  // ALCHEMY_METRICS_SALIGN_METRICS_HPP
