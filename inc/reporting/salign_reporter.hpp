// inc/salign_reporter.hpp
#ifndef ALCHEMY_REPORTING_SALIGN_REPORTER_HPP
#define ALCHEMY_REPORTING_SALIGN_REPORTER_HPP

// std
#include <cstddef>

#include <string>
#include <vector>

// local
#include "metrics/salign_metrics.hpp"

namespace alchemy::metrics::reporter {

namespace detail {

// common stats structure for alignment metrics
struct AlignmentStats {
  std::size_t totalSizeCurrent{0};
  std::size_t totalSizeOptimized{0};
  std::size_t totalDataSize{0};
  std::size_t wastedPaddingCurrent{0};
  std::size_t wastedPaddingOptimized{0};
  std::size_t maxAlignment{0};
  double avgCacheUtilCurrent{0.0};
  double avgCacheUtilOptimized{0.0};
};

struct FileStats : AlignmentStats {
  std::string file;
  std::size_t structCount{0};
};

struct GlobalStats : AlignmentStats {
  std::size_t totalStructs{0};
  std::size_t optimizableStructs{0};
};

// row data for metrics table rendering
struct MetricsRow {
  std::string label;
  std::string current;
  std::string optimized;
  std::string improvement;
};

// render a table section: column header + rows + bottom separator
void
printMetricsTable(const std::string& subtitle,
                  const std::vector<MetricsRow>& rows);

// helper: format percentage change for display
std::string
formatPercentageChange(std::size_t current, std::size_t optimized);

// helper: format padding improvement for display
std::string
formatPaddingImprovement(std::size_t currentPadding,
                         std::size_t optimizedPadding);

// helper: aggregate metrics by file
std::vector<FileStats>
aggregateByFile(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable);

// helper: report per-file summary
void
reportPerFileSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable);

// helper: sort metrics by savings (descending)
std::vector<alchemy::metrics::detail::SAlignMetrics>
sortBySavings(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics);

// helper: report per-struct summary
void
reportPerStructSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable);

// helper: compute global statistics
GlobalStats
computeGlobalStats(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable,
    std::size_t totalStructs);

// helper: report global summary
void
reportGlobalSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable,
    std::size_t totalStructs);

// helper: extract only optimizable structs from all metrics
std::vector<alchemy::metrics::detail::SAlignMetrics>
extractOptimizable(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& allMetrics);

// helper: accumulate common stats from a range of metrics
AlignmentStats
accumulateStats(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics);

}  // namespace detail

// salign-specific metrics reporter
class SAlignReporter {
public:
  static void
  report(const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics);
};

}  // namespace alchemy::metrics::reporter
#endif  // ALCHEMY_REPORTING_SALIGN_REPORTER_HPP
