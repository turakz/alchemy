// src/salign_reporter.cpp
#include "reporting/salign_reporter.hpp"

// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 3rd party
#include <fmt/core.h>

// local
#include "app/color.hpp"
#include "logger/logger.hpp"
#include "metrics/salign_metrics.hpp"

// render a table section: subtitle + column header + rows + bottom separator
void
alchemy::metrics::reporter::detail::printMetricsTable(
    std::string_view subtitle,
    const std::vector<alchemy::metrics::reporter::detail::MetricsRow>& rows)
{
  alchemy::logger::info("\n{}\n", subtitle);
  alchemy::logger::info("{:─<80}\n", "");
  alchemy::logger::info("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                        "Metric",
                        "Current",
                        "Optimized",
                        "Improvement");
  alchemy::logger::info("{:─<80}\n", "");
  for (const auto& row : rows)
  {
    alchemy::logger::info("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                          row.label,
                          row.current,
                          row.optimized,
                          row.improvement);
  }
  alchemy::logger::info("{:─<80}\n", "");
}

// helper: format percentage change for display
std::string
alchemy::metrics::reporter::detail::formatPercentageChange(
    std::size_t current, std::size_t optimized)
{
  if (current == 0)
  {
    return fmt::format("{}(n/a){}",
                       alchemy::color::ansi::Magenta,
                       alchemy::color::ansi::Reset);
  }

  std::size_t change =
      current > optimized ? current - optimized : optimized - current;
  double percent = alchemy::metrics::calculatePercentage(change, current);

  if (optimized < current)
  {
    return fmt::format("{}-{} bytes (-{:.1f}%){}",
                       alchemy::color::ansi::BrightGreen,
                       change,
                       percent,
                       alchemy::color::ansi::Reset);
  }
  if (optimized > current)
  {
    return fmt::format("{}+{} bytes (+{:.1f}%){}",
                       alchemy::color::ansi::Magenta,
                       change,
                       percent,
                       alchemy::color::ansi::Reset);
  }
  return fmt::format(
      "{}(same){}", alchemy::color::ansi::Magenta, alchemy::color::ansi::Reset);
}

// helper: format padding improvement for display
std::string
alchemy::metrics::reporter::detail::formatPaddingImprovement(
    std::size_t currentPadding, std::size_t optimizedPadding)
{
  if (optimizedPadding > currentPadding)
  {
    std::size_t increase = optimizedPadding - currentPadding;
    double percent =
        alchemy::metrics::calculatePercentage(increase, currentPadding);
    return fmt::format("{}+{} bytes (+{:.1f}%){}",
                       alchemy::color::ansi::Magenta,
                       increase,
                       percent,
                       alchemy::color::ansi::Reset);
  }
  if (optimizedPadding < currentPadding)
  {
    std::size_t decrease = currentPadding - optimizedPadding;
    double percent =
        alchemy::metrics::calculatePercentage(decrease, currentPadding);
    return fmt::format("{}-{} bytes (-{:.1f}%){}",
                       alchemy::color::ansi::BrightGreen,
                       decrease,
                       percent,
                       alchemy::color::ansi::Reset);
  }
  return fmt::format(
      "{}(same){}", alchemy::color::ansi::Magenta, alchemy::color::ansi::Reset);
}

// helper: accumulate common stats from a range of metrics
alchemy::metrics::reporter::detail::AlignmentStats
alchemy::metrics::reporter::detail::accumulateStats(
    const std::vector<alchemy::metrics::SAlignMetrics>& metrics)
{
  alchemy::metrics::reporter::detail::AlignmentStats stats;

  if (metrics.empty())
  {
    return stats;
  }

  double sumCacheUtilCurrent = 0.0;
  double sumCacheUtilOptimized = 0.0;
  for (const auto& m : metrics)
  {
    stats.totalSizeCurrent += m.naturalTotalSize;
    stats.totalSizeOptimized += m.optimizedSize;
    stats.totalDataSize += m.currentDataSize;
    stats.wastedPaddingCurrent += m.currentWastedBytes;
    stats.wastedPaddingOptimized += m.optimizedWaste;
    stats.maxAlignment = std::max(stats.maxAlignment, m.naturalAlignment);
    sumCacheUtilCurrent += m.currentCacheUtil;
    sumCacheUtilOptimized += m.optimizedCacheUtil;
  }
  const auto Count = static_cast<double>(metrics.size());
  stats.avgCacheUtilCurrent = sumCacheUtilCurrent / Count;
  stats.avgCacheUtilOptimized = sumCacheUtilOptimized / Count;

  return stats;
}

// helper: aggregate metrics by file
std::vector<alchemy::metrics::reporter::detail::FileStats>
alchemy::metrics::reporter::detail::aggregateByFile(
    const std::vector<alchemy::metrics::SAlignMetrics>& optimizable)
{
  std::unordered_map<std::string, std::vector<alchemy::metrics::SAlignMetrics>>
      byFile;
  for (const auto& metric : optimizable)
  {
    byFile[metric.sourceFile].push_back(metric);
  }

  std::vector<alchemy::metrics::reporter::detail::FileStats> result;
  for (const auto& [file, fileMetrics] : byFile)
  {
    const alchemy::metrics::reporter::detail::AlignmentStats Accumulated =
        alchemy::metrics::reporter::detail::accumulateStats(fileMetrics);

    alchemy::metrics::reporter::detail::FileStats stats;
    static_cast<AlignmentStats&>(stats) = Accumulated;
    stats.file = file;
    stats.structCount = fileMetrics.size();

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
    const std::vector<alchemy::metrics::SAlignMetrics>& optimizable)
{
  auto fileStats = detail::aggregateByFile(optimizable);

  alchemy::logger::info("\nalchemy::{}salign{}::by_file\n",
                        alchemy::color::ansi::BoldBrightGreen,
                        alchemy::color::ansi::Reset);
  alchemy::logger::info("{:═<80}\n", "");

  for (const auto& stats : fileStats)
  {
    alchemy::metrics::reporter::detail::printMetricsTable(
        fmt::format("{} ({} struct{})",
                    std::filesystem::path(stats.file).filename().string(),
                    stats.structCount,
                    stats.structCount == 1 ? "" : "s"),
        {{"Total Size",
          fmt::format("{} bytes", stats.totalSizeCurrent),
          fmt::format("{} bytes", stats.totalSizeOptimized),
          alchemy::metrics::reporter::detail::formatPercentageChange(
              stats.totalSizeCurrent, stats.totalSizeOptimized)},
         {"Wasted Padding",
          fmt::format("{} bytes", stats.wastedPaddingCurrent),
          fmt::format("{} bytes", stats.wastedPaddingOptimized),
          alchemy::metrics::reporter::detail::formatPaddingImprovement(
              stats.wastedPaddingCurrent, stats.wastedPaddingOptimized)}});
  }

  alchemy::logger::info("{:═<80}\n", "");
}

// helper: sort metrics by savings (descending)
std::vector<alchemy::metrics::SAlignMetrics>
alchemy::metrics::reporter::detail::sortBySavings(
    const std::vector<alchemy::metrics::SAlignMetrics>& metrics)
{
  auto sorted = metrics;
  std::sort(
      std::begin(sorted), std::end(sorted), [](const auto& a, const auto& b) {
        return a.possibleSavings > b.possibleSavings;
      });
  return sorted;
}

// helper: report per-struct summary
void
alchemy::metrics::reporter::detail::reportPerStructSummary(
    const std::vector<alchemy::metrics::SAlignMetrics>& optimizable)
{
  auto sorted = alchemy::metrics::reporter::detail::sortBySavings(optimizable);
  std::size_t displayCount =
      std::min(static_cast<std::size_t>(10), sorted.size());

  alchemy::logger::info(
      "\nalchemy::{}salign{}::by_struct (top {} optimization opportunities)\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      displayCount);
  alchemy::logger::info("{:═<80}\n", "");

  for (std::size_t i = 0; i < displayCount; ++i)
  {
    const auto& m = sorted[i];
    std::vector<alchemy::metrics::reporter::detail::MetricsRow> rows = {
        {"Total Size",
         fmt::format("{} bytes", m.naturalTotalSize),
         fmt::format("{} bytes", m.optimizedSize),
         alchemy::metrics::reporter::detail::formatPercentageChange(
             m.naturalTotalSize, m.optimizedSize)},
        {"Wasted Padding",
         fmt::format("{} bytes", m.currentWastedBytes),
         fmt::format("{} bytes", m.optimizedWaste),
         alchemy::metrics::reporter::detail::formatPaddingImprovement(
             m.currentWastedBytes, m.optimizedWaste)},
        {"Cache Util",
         fmt::format("{:.1f}%", m.currentCacheUtil),
         fmt::format("{:.1f}%", m.optimizedCacheUtil),
         fmt::format("{:+.1f}%", m.optimizedCacheUtil - m.currentCacheUtil)}};

    if (m.optimizedCacheSize > m.optimizedSize)
    {
      rows.push_back({"Array Hint",
                      "─",
                      fmt::format("{} bytes", m.optimizedCacheSize),
                      fmt::format("multiples of {}",
                                  m.optimizedCacheSize / m.optimizedSize)});
    }

    alchemy::metrics::reporter::detail::printMetricsTable(
        fmt::format("{} ({})",
                    m.structName,
                    std::filesystem::path(m.sourceFile).filename().string()),
        rows);
  }

  alchemy::logger::info("{:═<80}\n", "");
}

// helper: compute global statistics
alchemy::metrics::reporter::detail::GlobalStats
alchemy::metrics::reporter::detail::computeGlobalStats(
    const std::vector<alchemy::metrics::SAlignMetrics>& optimizable,
    std::size_t totalStructs)
{
  const alchemy::metrics::reporter::detail::AlignmentStats Accumulated =
      alchemy::metrics::reporter::detail::accumulateStats(optimizable);

  alchemy::metrics::reporter::detail::GlobalStats stats;
  static_cast<AlignmentStats&>(stats) = Accumulated;
  stats.totalStructs = totalStructs;
  stats.optimizableStructs = optimizable.size();

  return stats;
}

// helper: report global summary
void
alchemy::metrics::reporter::detail::reportGlobalSummary(
    const std::vector<alchemy::metrics::SAlignMetrics>& optimizable,
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

  alchemy::logger::info(
      "\nalchemy::{}salign{}::summary (analyzed {} structs, {} optimizable)\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      stats.totalStructs,
      stats.optimizableStructs);
  alchemy::logger::info("{:═<80}\n", "");
  // global summary uses ═ separators, so we print column header + rows inline
  // rather than through printMetricsTable (which uses ─ separators)
  alchemy::logger::info("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                        "Metric",
                        "Current",
                        "Optimized",
                        "Improvement");
  alchemy::logger::info("{:─<80}\n", "");
  alchemy::logger::info("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                        "Total Size",
                        fmt::format("{} bytes", stats.totalSizeCurrent),
                        fmt::format("{} bytes", stats.totalSizeOptimized),
                        sizeChange);
  alchemy::logger::info("{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
                        "Wasted Padding",
                        fmt::format("{} bytes", stats.wastedPaddingCurrent),
                        fmt::format("{} bytes", stats.wastedPaddingOptimized),
                        paddingImprovement);
  alchemy::logger::info(
      "{:<18} │ {:>12} │ {:>12} │ {:<30}\n",
      "Avg Cache Util",
      fmt::format("{:.1f}%", stats.avgCacheUtilCurrent),
      fmt::format("{:.1f}%", stats.avgCacheUtilOptimized),
      fmt::format("{:+.1f}%",
                  stats.avgCacheUtilOptimized - stats.avgCacheUtilCurrent));
  alchemy::logger::info("{:═<80}\n", "");
  alchemy::logger::info("\n");
}

// helper: extract only optimizable structs from all metrics
std::vector<alchemy::metrics::SAlignMetrics>
alchemy::metrics::reporter::detail::extractOptimizable(
    const std::vector<alchemy::metrics::SAlignMetrics>& allMetrics)
{
  std::vector<alchemy::metrics::SAlignMetrics> result;
  std::copy_if(allMetrics.begin(),
               allMetrics.end(),
               std::back_inserter(result),
               [](const auto& m) { return m.possibleSavings > 0; });
  return result;
}

void
alchemy::metrics::reporter::SAlignReporter::report(
    const std::vector<alchemy::metrics::SAlignMetrics>& metrics)
{
  if (metrics.empty())
  {
    alchemy::logger::info(
        "alchemy::{}salign{}::{}no optimization opportunities found{}\n",
        alchemy::color::ansi::BoldBrightGreen,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Magenta,
        alchemy::color::ansi::Reset);
    return;
  }

  // report skipped structs (e.g., #pragma pack)
  const auto SkippedCount = std::count_if(
      metrics.begin(), metrics.end(), [](const auto& m) { return m.skipped; });
  if (SkippedCount > 0)
  {
    alchemy::logger::info(
        "alchemy::{}salign{}::skipped {}{}{} {}#pragma packed{} struct{} "
        "(field order defines binary layout)\n"
        "alchemy::{}hint{}: remove #pragma pack before running alchemy if "
        "the struct is not serialized\n",
        alchemy::color::ansi::BoldBrightGreen,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Yellow,
        SkippedCount,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Yellow,
        alchemy::color::ansi::Reset,
        SkippedCount == 1 ? "" : "s",
        alchemy::color::ansi::Cyan,
        alchemy::color::ansi::Reset);
  }

  auto optimizable =
      alchemy::metrics::reporter::detail::extractOptimizable(metrics);

  if (optimizable.empty())
  {
    alchemy::logger::info(
        "alchemy::{}salign{}::all {} structs already {}optimized{}\n",
        alchemy::color::ansi::BoldBrightGreen,
        alchemy::color::ansi::Reset,
        metrics.size(),
        alchemy::color::ansi::BoldBrightGreen,
        alchemy::color::ansi::Reset);
    return;
  }

  // file → struct → global
  alchemy::metrics::reporter::detail::reportPerFileSummary(optimizable);
  alchemy::metrics::reporter::detail::reportPerStructSummary(optimizable);
  alchemy::metrics::reporter::detail::reportGlobalSummary(optimizable,
                                                          metrics.size());
}
