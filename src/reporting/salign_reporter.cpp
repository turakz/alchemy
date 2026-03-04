// src/salign_reporter.cpp
// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

// 3rd party
#include "app/color.hpp"
#include "fmt/core.h"
#include "metrics/salign_metrics.hpp"

// local
#include "reporting/salign_reporter.hpp"

// helper: format percentage change for display
std::string
alchemy::metrics::reporter::detail::formatPercentageChange(
    std::size_t current, std::size_t optimized)
{
  if (current == 0)
  {
    return "(n/a)";
  }

  std::size_t change =
      current > optimized ? current - optimized : optimized - current;
  double percent =
      (static_cast<double>(change) / static_cast<double>(current)) * 100.0;

  if (optimized < current)
  {
    return fmt::format("-{} bytes (-{:.1f}%)", change, percent);
  }
  if (optimized > current)
  {
    return fmt::format("+{} bytes (+{:.1f}%)", change, percent);
  }
  return "(same)";
}

// helper: format padding improvement for display
std::string
alchemy::metrics::reporter::detail::formatPaddingImprovement(
    std::size_t currentPadding, std::size_t optimizedPadding)
{
  if (optimizedPadding > currentPadding)
  {
    std::size_t increase = optimizedPadding - currentPadding;
    double percent = currentPadding > 0
                         ? (static_cast<double>(increase) /
                            static_cast<double>(currentPadding)) *
                               100.0
                         : 0.0;
    return fmt::format("+{} bytes (+{:.1f}%)", increase, percent);
  }
  if (optimizedPadding < currentPadding)
  {
    std::size_t decrease = currentPadding - optimizedPadding;
    double percent = currentPadding > 0
                         ? (static_cast<double>(decrease) /
                            static_cast<double>(currentPadding)) *
                               100.0
                         : 0.0;
    return fmt::format("-{} bytes (-{:.1f}%)", decrease, percent);
  }
  return "(same)";
}

// helper: accumulate common stats from a range of metrics
alchemy::metrics::reporter::detail::AlignmentStats
alchemy::metrics::reporter::detail::accumulateStats(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics)
{
  alchemy::metrics::reporter::detail::AlignmentStats stats;

  if (metrics.empty())
  {
    return stats;
  }

  stats.totalSizeCurrent = std::accumulate(
      metrics.begin(),
      metrics.end(),
      static_cast<std::size_t>(0),
      [](auto sum, const auto& m) { return sum + m.naturalTotalSize; });

  stats.totalSizeOptimized = std::accumulate(
      metrics.begin(),
      metrics.end(),
      static_cast<std::size_t>(0),
      [](auto sum, const auto& m) { return sum + m.optimizedSize; });

  stats.totalDataSize = std::accumulate(
      metrics.begin(),
      metrics.end(),
      static_cast<std::size_t>(0),
      [](auto sum, const auto& m) { return sum + m.currentDataSize; });

  stats.wastedPaddingCurrent = std::accumulate(
      metrics.begin(),
      metrics.end(),
      static_cast<std::size_t>(0),
      [](auto sum, const auto& m) { return sum + m.currentWastedBytes; });

  stats.wastedPaddingOptimized = std::accumulate(
      metrics.begin(),
      metrics.end(),
      static_cast<std::size_t>(0),
      [](auto sum, const auto& m) { return sum + m.optimizedWaste; });

  stats.maxAlignment =
      std::accumulate(metrics.begin(),
                      metrics.end(),
                      static_cast<std::size_t>(0),
                      [](auto maxAlign, const auto& m) {
                        return std::max(maxAlign, m.naturalAlignment);
                      });

  const double count = static_cast<double>(metrics.size());
  stats.avgCacheUtilCurrent = std::accumulate(metrics.begin(),
                                              metrics.end(),
                                              0.0,
                                              [](double sum, const auto& m) {
                                                return sum + m.currentCacheUtil;
                                              }) /
                              count;

  stats.avgCacheUtilOptimized =
      std::accumulate(metrics.begin(),
                      metrics.end(),
                      0.0,
                      [](double sum, const auto& m) {
                        return sum + m.optimizedCacheUtil;
                      }) /
      count;

  return stats;
}

// helper: aggregate metrics by file
std::vector<alchemy::metrics::reporter::detail::FileStats>
alchemy::metrics::reporter::detail::aggregateByFile(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable)
{
  std::unordered_map<std::filesystem::path,
                     std::vector<alchemy::metrics::detail::SAlignMetrics>>
      byFile;
  for (const auto& metric : optimizable)
  {
    byFile[metric.sourceFile].push_back(metric);
  }

  std::vector<alchemy::metrics::reporter::detail::FileStats> result;
  for (const auto& [file, fileMetrics] : byFile)
  {
    alchemy::metrics::reporter::detail::AlignmentStats accumulated =
        alchemy::metrics::reporter::detail::accumulateStats(fileMetrics);

    alchemy::metrics::reporter::detail::FileStats stats;
    stats.file = file;
    stats.structCount = fileMetrics.size();
    stats.totalSizeCurrent = accumulated.totalSizeCurrent;
    stats.totalSizeOptimized = accumulated.totalSizeOptimized;
    stats.totalDataSize = accumulated.totalDataSize;
    stats.wastedPaddingCurrent = accumulated.wastedPaddingCurrent;
    stats.wastedPaddingOptimized = accumulated.wastedPaddingOptimized;
    stats.maxAlignment = accumulated.maxAlignment;
    stats.avgCacheUtilCurrent = accumulated.avgCacheUtilCurrent;
    stats.avgCacheUtilOptimized = accumulated.avgCacheUtilOptimized;

    result.push_back(stats);
  }

  // Sort by total size savings descending
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return (a.totalSizeCurrent - a.totalSizeOptimized) >
           (b.totalSizeCurrent - b.totalSizeOptimized);
  });

  return result;
}

// helper: report per-file summary
void
alchemy::metrics::reporter::detail::reportPerFileSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable)
{
  auto fileStats = detail::aggregateByFile(optimizable);

  fmt::print("\nalchemy::{}salign{}::by_file\n",
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);
  fmt::print("{:═<80}\n", "");

  for (const auto& stats : fileStats)
  {
    std::string sizeChange =
        alchemy::metrics::reporter::detail::formatPercentageChange(
            stats.totalSizeCurrent, stats.totalSizeOptimized);
    std::string paddingImprovement =
        alchemy::metrics::reporter::detail::formatPaddingImprovement(
            stats.wastedPaddingCurrent, stats.wastedPaddingOptimized);

    fmt::print("\n{} ({} struct{})\n",
               stats.file.filename().string(),
               stats.structCount,
               stats.structCount == 1 ? "" : "s");
    fmt::print("{:─<80}\n", "");
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Metric",
               "Current",
               "Optimized",
               "Improvement");
    fmt::print("{:─<80}\n", "");
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Total Size",
               fmt::format("{} bytes", stats.totalSizeCurrent),
               fmt::format("{} bytes", stats.totalSizeOptimized),
               sizeChange);
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Wasted Padding",
               fmt::format("{} bytes", stats.wastedPaddingCurrent),
               fmt::format("{} bytes", stats.wastedPaddingOptimized),
               paddingImprovement);
    fmt::print("{:─<80}\n", "");
  }

  fmt::print("{:═<80}\n", "");
}

// helper: sort metrics by savings (descending)
std::vector<alchemy::metrics::detail::SAlignMetrics>
alchemy::metrics::reporter::detail::sortBySavings(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics)
{
  auto sorted = metrics;
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a.possibleSavings > b.possibleSavings;
  });
  return sorted;
}

// helper: report per-struct summary
void
alchemy::metrics::reporter::detail::reportPerStructSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable)
{
  auto sorted = alchemy::metrics::reporter::detail::sortBySavings(optimizable);
  std::size_t displayCount =
      std::min(static_cast<std::size_t>(10), sorted.size());

  fmt::print(
      "\nalchemy::{}salign{}::by_struct (top {} optimization opportunities)\n",
      alchemy::color::ansi::BrightGreen,
      alchemy::color::ansi::Reset,
      displayCount);
  fmt::print("{:═<80}\n", "");

  for (std::size_t i = 0; i < displayCount; ++i)
  {
    const auto& m = sorted[i];
    std::string sizeChange =
        alchemy::metrics::reporter::detail::formatPercentageChange(
            m.naturalTotalSize, m.optimizedSize);
    std::string paddingImprovement =
        alchemy::metrics::reporter::detail::formatPaddingImprovement(
            m.currentWastedBytes, m.optimizedWaste);

    fmt::print("\n{} ({})\n", m.structName, m.sourceFile.filename().string());
    fmt::print("{:─<80}\n", "");
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Metric",
               "Current",
               "Optimized",
               "Improvement");
    fmt::print("{:─<80}\n", "");
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Total Size",
               fmt::format("{} bytes", m.naturalTotalSize),
               fmt::format("{} bytes", m.optimizedSize),
               sizeChange);
    fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
               "Wasted Padding",
               fmt::format("{} bytes", m.currentWastedBytes),
               fmt::format("{} bytes", m.optimizedWaste),
               paddingImprovement);

    // Cache metrics (NEW)
    fmt::print(
        "{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
        "SPCL Score",
        fmt::format("{:.1f}/line", m.currentSpclScore),
        fmt::format("{:.1f}/line", m.optimizedSpclScore),
        fmt::format("+{:.1f}/line", m.optimizedSpclScore - m.currentSpclScore));
    fmt::print(
        "{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
        "Cache Util",
        fmt::format("{:.1f}%", m.currentCacheUtil),
        fmt::format("{:.1f}%", m.optimizedCacheUtil),
        fmt::format("+{:.1f}%", m.optimizedCacheUtil - m.currentCacheUtil));

    // Array allocation hint (only show if > 1)
    if (m.optimizedCacheSize > m.optimizedSize)
    {
      fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                 "Array Hint",
                 "─",
                 fmt::format("{} bytes", m.optimizedCacheSize),
                 fmt::format("multiples of {}",
                             m.optimizedCacheSize / m.optimizedSize));
    }

    fmt::print("{:─<80}\n", "");
  }

  fmt::print("{:═<80}\n", "");
}

// helper: compute global statistics
alchemy::metrics::reporter::detail::GlobalStats
alchemy::metrics::reporter::detail::computeGlobalStats(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable,
    std::size_t totalStructs)
{
  alchemy::metrics::reporter::detail::AlignmentStats accumulated =
      alchemy::metrics::reporter::detail::accumulateStats(optimizable);

  alchemy::metrics::reporter::detail::GlobalStats stats;
  stats.totalStructs = totalStructs;
  stats.optimizableStructs = optimizable.size();
  stats.totalSizeCurrent = accumulated.totalSizeCurrent;
  stats.totalSizeOptimized = accumulated.totalSizeOptimized;
  stats.totalDataSize = accumulated.totalDataSize;
  stats.wastedPaddingCurrent = accumulated.wastedPaddingCurrent;
  stats.wastedPaddingOptimized = accumulated.wastedPaddingOptimized;
  stats.maxAlignment = accumulated.maxAlignment;
  stats.avgCacheUtilCurrent = accumulated.avgCacheUtilCurrent;
  stats.avgCacheUtilOptimized = accumulated.avgCacheUtilOptimized;

  return stats;
}

// helper: report global summary
void
alchemy::metrics::reporter::detail::reportGlobalSummary(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& optimizable,
    std::size_t totalStructs)
{
  auto stats = alchemy::metrics::reporter::detail::computeGlobalStats(
      optimizable, totalStructs);

  std::string sizeChange =
      alchemy::metrics::reporter::detail::formatPercentageChange(
          stats.totalSizeCurrent, stats.totalSizeOptimized);
  std::string paddingImprovement =
      alchemy::metrics::reporter::detail::formatPaddingImprovement(
          stats.wastedPaddingCurrent, stats.wastedPaddingOptimized);

  fmt::print(
      "\nalchemy::{}salign{}::summary (analyzed {} structs, {} optimizable)\n",
      alchemy::color::ansi::BrightGreen,
      alchemy::color::ansi::Reset,
      stats.totalStructs,
      stats.optimizableStructs);
  fmt::print("{:═<80}\n", "");
  fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
             "Metric",
             "Current",
             "Optimized",
             "Improvement");
  fmt::print("{:─<80}\n", "");
  fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
             "Total Size",
             fmt::format("{} bytes", stats.totalSizeCurrent),
             fmt::format("{} bytes", stats.totalSizeOptimized),
             sizeChange);
  fmt::print("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
             "Wasted Padding",
             fmt::format("{} bytes", stats.wastedPaddingCurrent),
             fmt::format("{} bytes", stats.wastedPaddingOptimized),
             paddingImprovement);
  fmt::print(
      "{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
      "Avg Cache Util",
      fmt::format("{:.1f}%", stats.avgCacheUtilCurrent),
      fmt::format("{:.1f}%", stats.avgCacheUtilOptimized),
      fmt::format("+{:.1f}%",
                  stats.avgCacheUtilOptimized - stats.avgCacheUtilCurrent));
  fmt::print("{:═<80}\n", "");
  fmt::print("\n");
}

// helper: extract only optimizable structs from all metrics
std::vector<alchemy::metrics::detail::SAlignMetrics>
alchemy::metrics::reporter::detail::extractOptimizable(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& allMetrics)
{
  std::vector<alchemy::metrics::detail::SAlignMetrics> result;
  std::copy_if(allMetrics.begin(),
               allMetrics.end(),
               std::back_inserter(result),
               [](const auto& m) { return m.possibleSavings > 0; });
  return result;
}

void
alchemy::metrics::reporter::SAlignReporter::report(
    const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics)
{
  if (metrics.empty())
  {
    fmt::print("alchemy::{}salign{}::{}no optimization opportunities found{}\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::Magenta,
               alchemy::color::ansi::Reset);
    return;
  }

  auto optimizable =
      alchemy::metrics::reporter::detail::extractOptimizable(metrics);

  if (optimizable.empty())
  {
    fmt::print("alchemy::{}salign{}::all {} structs already {}optimized{}\n"
               "alchemy::{}tip{}: if your structs are #pragma packed, consider "
               "temporarily removing the preprocessor for structs that "
               "encapsulate other structs, and then re-running alchemy\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               metrics.size(),
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::Cyan,
               alchemy::color::ansi::Reset);
    return;
  }

  // file → struct → global
  alchemy::metrics::reporter::detail::reportPerFileSummary(optimizable);
  alchemy::metrics::reporter::detail::reportPerStructSummary(optimizable);
  alchemy::metrics::reporter::detail::reportGlobalSummary(optimizable,
                                                          metrics.size());
}
