// tests/unit/test_salign_reporter.cpp

// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "metrics/metrics.hpp"
#include "metrics/salign_metrics.hpp"
#include "reporting/metrics_reporter.hpp"
#include "reporting/salign_reporter.hpp"

namespace alchemy::testing {

class SAlignReporterTest : public ::testing::Test {
protected:
  // helper to create a sample SAlignMetrics
  alchemy::metrics::SAlignMetrics
  createMetrics(const std::string& structName,
                const std::string& fileName,
                std::size_t naturalSize,
                std::size_t optimizedSize,
                std::size_t currentWaste,
                std::size_t optimizedWaste)
  {
    alchemy::metrics::SAlignMetrics m;
    m.sourceFile = fileName;
    m.structName = structName;
    m.naturalTotalSize = naturalSize;
    m.optimizedSize = optimizedSize;
    m.currentDataSize = naturalSize - currentWaste;
    m.currentWastedBytes = currentWaste;
    m.optimizedWaste = optimizedWaste;
    m.possibleSavings =
        naturalSize > optimizedSize ? naturalSize - optimizedSize : 0;
    m.naturalAlignment = 8;
    m.currentCacheUtil = 50.0;
    m.optimizedCacheUtil = 75.0;
    return m;
  }
};

// ============================================================================
// FEATURE: formatPercentageChange
// ============================================================================

TEST_F(SAlignReporterTest, FormatPercentageChangeHandlesDecrease)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPercentageChange(100, 80);

  ASSERT_EQ(result, "\033[92m-20 bytes (-20.0%)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPercentageChangeHandlesIncrease)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPercentageChange(80, 100);

  ASSERT_EQ(result, "\033[35m+20 bytes (+25.0%)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPercentageChangeHandlesSame)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPercentageChange(100, 100);

  ASSERT_EQ(result, "\033[35m(same)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPercentageChangeHandlesZeroCurrent)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPercentageChange(0, 50);

  ASSERT_EQ(result, "\033[35m(n/a)\033[0m");
}

// ============================================================================
// FEATURE: formatPaddingImprovement
// ============================================================================

TEST_F(SAlignReporterTest, FormatPaddingImprovementHandlesDecrease)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPaddingImprovement(20, 10);

  ASSERT_EQ(result, "\033[92m-10 bytes (-50.0%)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPaddingImprovementHandlesIncrease)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPaddingImprovement(10, 20);

  ASSERT_EQ(result, "\033[35m+10 bytes (+100.0%)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPaddingImprovementHandlesSame)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPaddingImprovement(15, 15);

  ASSERT_EQ(result, "\033[35m(same)\033[0m");
}

TEST_F(SAlignReporterTest, FormatPaddingImprovementHandlesZeroCurrent)
{
  auto result =
      alchemy::metrics::reporter::detail::formatPaddingImprovement(0, 10);

  ASSERT_EQ(result, "\033[35m+10 bytes (+0.0%)\033[0m");
}

// ============================================================================
// FEATURE: extractOptimizable
// ============================================================================

TEST_F(SAlignReporterTest, ExtractOptimizableFiltersZeroSavings)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(
      createMetrics("Optimizable", "file.c", 100, 80, 20, 0));  // savings = 20
  metrics.push_back(
      createMetrics("NoSavings", "file.c", 80, 80, 0, 0));  // savings = 0
  metrics.push_back(createMetrics("AlsoOptimizable",
                                  "file.c",
                                  64,
                                  48,
                                  16,
                                  0));  // savings = 16

  auto result = alchemy::metrics::reporter::detail::extractOptimizable(metrics);

  ASSERT_EQ(result.size(), 2);
  ASSERT_EQ(result[0].structName, "Optimizable");
  ASSERT_EQ(result[1].structName, "AlsoOptimizable");
}

TEST_F(SAlignReporterTest, ExtractOptimizableHandlesEmptyInput)
{
  const std::vector<alchemy::metrics::SAlignMetrics> Empty;

  auto result = alchemy::metrics::reporter::detail::extractOptimizable(Empty);

  ASSERT_TRUE(result.empty());
}

TEST_F(SAlignReporterTest, ExtractOptimizableHandlesAllOptimizable)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("Struct1", "file.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("Struct2", "file.c", 64, 48, 16, 0));

  auto result = alchemy::metrics::reporter::detail::extractOptimizable(metrics);

  ASSERT_EQ(result.size(), 2);
}

// ============================================================================
// FEATURE: sortBySavings
// ============================================================================

TEST_F(SAlignReporterTest, SortBySavingsSortsDescending)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(
      createMetrics("SmallSavings", "file.c", 100, 90, 10, 0));  // 10 bytes
  metrics.push_back(
      createMetrics("LargeSavings", "file.c", 128, 64, 64, 0));  // 64 bytes
  metrics.push_back(
      createMetrics("MediumSavings", "file.c", 80, 60, 20, 0));  // 20 bytes

  auto sorted = alchemy::metrics::reporter::detail::sortBySavings(metrics);

  ASSERT_EQ(sorted.size(), 3);
  ASSERT_EQ(sorted[0].structName, "LargeSavings");
  ASSERT_EQ(sorted[0].possibleSavings, 64);
  ASSERT_EQ(sorted[1].structName, "MediumSavings");
  ASSERT_EQ(sorted[1].possibleSavings, 20);
  ASSERT_EQ(sorted[2].structName, "SmallSavings");
  ASSERT_EQ(sorted[2].possibleSavings, 10);
}

TEST_F(SAlignReporterTest, SortBySavingsHandlesEmptyInput)
{
  const std::vector<alchemy::metrics::SAlignMetrics> Empty;

  auto sorted = alchemy::metrics::reporter::detail::sortBySavings(Empty);

  ASSERT_TRUE(sorted.empty());
}

// ============================================================================
// FEATURE: aggregateByFile
// ============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(SAlignReporterTest, AggregateByFileGroupsByFilename)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("Struct1", "file1.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("Struct2", "file1.c", 64, 48, 16, 0));
  metrics.push_back(createMetrics("Struct3", "file2.c", 128, 96, 32, 0));

  auto fileStats = alchemy::metrics::reporter::detail::aggregateByFile(metrics);

  ASSERT_EQ(fileStats.size(), 2);

  // find file1.c stats
  auto file1It = std::find_if(
      std::begin(fileStats), std::end(fileStats), [](const auto& fs) {
        return std::filesystem::path(fs.file).filename() == "file1.c";
      });
  ASSERT_NE(file1It, std::end(fileStats));

  ASSERT_EQ(file1It->structCount, 2);
  ASSERT_EQ(file1It->totalSizeCurrent, 164);     // 100 + 64
  ASSERT_EQ(file1It->totalSizeOptimized, 128);   // 80 + 48
  ASSERT_EQ(file1It->totalDataSize, 128);        // (100-20) + (64-16)
  ASSERT_EQ(file1It->wastedPaddingCurrent, 36);  // 20 + 16

  // find file2.c stats
  auto file2It = std::find_if(
      std::begin(fileStats), std::end(fileStats), [](const auto& fs) {
        return std::filesystem::path(fs.file).filename() == "file2.c";
      });
  ASSERT_NE(file2It, std::end(fileStats));

  ASSERT_EQ(file2It->structCount, 1);
  ASSERT_EQ(file2It->totalSizeCurrent, 128);
  ASSERT_EQ(file2It->totalSizeOptimized, 96);
}

TEST_F(SAlignReporterTest, AggregateByFileSortsBySavingsDescending)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  // file1: small savings (10 bytes total)
  metrics.push_back(createMetrics("Struct1", "file1.c", 100, 90, 10, 0));
  // file2: large savings (64 bytes total)
  metrics.push_back(createMetrics("Struct2", "file2.c", 128, 64, 64, 0));
  // file3: medium savings (20 bytes total)
  metrics.push_back(createMetrics("Struct3", "file3.c", 80, 60, 20, 0));

  auto fileStats = alchemy::metrics::reporter::detail::aggregateByFile(metrics);

  ASSERT_EQ(fileStats.size(), 3);
  // sorted by savings descending: file2 (64), file3 (20), file1 (10)
  ASSERT_EQ(std::filesystem::path(fileStats[0].file).filename(), "file2.c");
  ASSERT_EQ(fileStats[0].totalSizeCurrent - fileStats[0].totalSizeOptimized,
            64);
  ASSERT_EQ(std::filesystem::path(fileStats[1].file).filename(), "file3.c");
  ASSERT_EQ(fileStats[1].totalSizeCurrent - fileStats[1].totalSizeOptimized,
            20);
  ASSERT_EQ(std::filesystem::path(fileStats[2].file).filename(), "file1.c");
  ASSERT_EQ(fileStats[2].totalSizeCurrent - fileStats[2].totalSizeOptimized,
            10);
}

TEST_F(SAlignReporterTest, AggregateByFileHandlesEmptyInput)
{
  const std::vector<alchemy::metrics::SAlignMetrics> Empty;

  auto fileStats = alchemy::metrics::reporter::detail::aggregateByFile(Empty);

  ASSERT_TRUE(fileStats.empty());
}

// ============================================================================
// FEATURE: computeGlobalStats
// ============================================================================

TEST_F(SAlignReporterTest, ComputeGlobalStatsAggregatesMetrics)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("Struct1", "file.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("Struct2", "file.c", 64, 48, 16, 0));
  metrics.push_back(createMetrics("Struct3", "file.c", 128, 96, 32, 0));

  auto stats =
      alchemy::metrics::reporter::detail::computeGlobalStats(metrics, 5);

  ASSERT_EQ(stats.totalStructs, 5);            // total analyzed
  ASSERT_EQ(stats.optimizableStructs, 3);      // optimizable count
  ASSERT_EQ(stats.totalSizeCurrent, 292);      // 100 + 64 + 128
  ASSERT_EQ(stats.totalSizeOptimized, 224);    // 80 + 48 + 96
  ASSERT_EQ(stats.totalDataSize, 224);         // (100-20) + (64-16) + (128-32)
  ASSERT_EQ(stats.wastedPaddingCurrent, 68);   // 20 + 16 + 32
  ASSERT_EQ(stats.wastedPaddingOptimized, 0);  // all zero in test data
  ASSERT_EQ(stats.maxAlignment, 8);            // max across all
}

TEST_F(SAlignReporterTest, ComputeGlobalStatsCalculatesAverageCacheUtil)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  // cache utils: current 50%, optimized 75%
  metrics.push_back(createMetrics("Struct1", "file.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("Struct2", "file.c", 64, 48, 16, 0));

  auto stats =
      alchemy::metrics::reporter::detail::computeGlobalStats(metrics, 2);

  ASSERT_DOUBLE_EQ(stats.avgCacheUtilCurrent, 50.0);    // avg of 50.0, 50.0
  ASSERT_DOUBLE_EQ(stats.avgCacheUtilOptimized, 75.0);  // avg of 75.0, 75.0
}

TEST_F(SAlignReporterTest, ComputeGlobalStatsHandlesSingleMetric)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("OnlyStruct", "file.c", 100, 80, 20, 0));

  auto stats =
      alchemy::metrics::reporter::detail::computeGlobalStats(metrics, 1);

  ASSERT_EQ(stats.totalStructs, 1);
  ASSERT_EQ(stats.optimizableStructs, 1);
  ASSERT_EQ(stats.totalSizeCurrent, 100);
  ASSERT_EQ(stats.totalSizeOptimized, 80);
}

// ============================================================================
// FEATURE: accumulateStats
// ============================================================================

TEST_F(SAlignReporterTest, AccumulateStatsHandlesEmptyInput)
{
  const std::vector<alchemy::metrics::SAlignMetrics> Empty;

  auto stats = alchemy::metrics::reporter::detail::accumulateStats(Empty);

  ASSERT_EQ(stats.totalSizeCurrent, 0)
      << "alchemy::testing::unit::empty input should yield zero total size";
  ASSERT_EQ(stats.totalSizeOptimized, 0);
  ASSERT_EQ(stats.totalDataSize, 0);
  ASSERT_EQ(stats.wastedPaddingCurrent, 0);
  ASSERT_EQ(stats.wastedPaddingOptimized, 0);
  ASSERT_EQ(stats.maxAlignment, 0);
  ASSERT_DOUBLE_EQ(stats.avgCacheUtilCurrent, 0.0);
  ASSERT_DOUBLE_EQ(stats.avgCacheUtilOptimized, 0.0);
}

// ============================================================================
// FEATURE: reportMetrics (variant dispatcher)
// ============================================================================

TEST_F(SAlignReporterTest, ReportMetricsHandlesEmptyInput)
{
  const std::vector<alchemy::metrics::Metrics> Empty;

  ASSERT_NO_THROW(alchemy::metrics::reporter::reportMetrics(Empty))
      << "alchemy::testing::unit::reportMetrics should handle empty input";
}

TEST_F(SAlignReporterTest, ReportMetricsDispatchesSAlignMetrics)
{
  std::vector<alchemy::metrics::Metrics> metrics;
  metrics.push_back(createMetrics("TestStruct", "test.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("TestStruct2", "test.c", 64, 48, 16, 0));

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::reportMetrics(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_FALSE(output.empty())
      << "alchemy::testing::unit::reportMetrics should produce output for "
         "SAlignMetrics";
  ASSERT_NE(output.find("salign"), std::string::npos)
      << "alchemy::testing::unit::output should contain salign report content";
}

TEST_F(SAlignReporterTest, ReportMetricsHandlesSingleMetric)
{
  std::vector<alchemy::metrics::Metrics> metrics;
  metrics.push_back(createMetrics("OnlyStruct", "test.c", 100, 80, 20, 0));

  ::testing::internal::CaptureStdout();
  ASSERT_NO_THROW(alchemy::metrics::reporter::reportMetrics(metrics))
      << "alchemy::testing::unit::reportMetrics should handle single metric";
  ::testing::internal::GetCapturedStdout();
}

// ============================================================================
// FEATURE: SAlignReporter::report (top-level reporter)
// ============================================================================

TEST_F(SAlignReporterTest, ReportHandlesEmptyMetrics)
{
  const std::vector<alchemy::metrics::SAlignMetrics> Empty;

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(Empty);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_NE(output.find("no optimization opportunities"), std::string::npos)
      << "alchemy::testing::unit::empty metrics should report no opportunities";
}

TEST_F(SAlignReporterTest, ReportHandlesAllAlreadyOptimal)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  // possibleSavings == 0: already optimal
  metrics.push_back(createMetrics("Optimal1", "file.c", 80, 80, 0, 0));
  metrics.push_back(createMetrics("Optimal2", "file.c", 64, 64, 0, 0));

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_NE(output.find("already"), std::string::npos)
      << "alchemy::testing::unit::all-optimal metrics should report already "
         "optimized";
  ASSERT_NE(output.find("optimized"), std::string::npos)
      << "alchemy::testing::unit::output should mention optimized status";
}

TEST_F(SAlignReporterTest, ReportHandlesOptimizableMetrics)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("Struct1", "file1.c", 100, 80, 20, 0));
  metrics.push_back(createMetrics("Struct2", "file2.c", 128, 96, 32, 0));

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_FALSE(output.empty())
      << "alchemy::testing::unit::optimizable metrics should produce output";
  ASSERT_NE(output.find("by_file"), std::string::npos)
      << "alchemy::testing::unit::output should contain per-file summary";
  ASSERT_NE(output.find("by_struct"), std::string::npos)
      << "alchemy::testing::unit::output should contain per-struct summary";
}

TEST_F(SAlignReporterTest, ReportHandlesArrayHintMetrics)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  auto m = createMetrics("BigStruct", "file.c", 100, 12, 20, 0);
  // set optimizedCacheSize > optimizedSize to trigger Array Hint branch
  m.optimizedCacheSize = 192;
  metrics.push_back(m);

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_NE(output.find("Array Hint"), std::string::npos)
      << "output should contain Array Hint when optimizedCacheSize > "
         "optimizedSize";
}

// ============================================================================
// FEATURE: skipped struct reporting
// ============================================================================

TEST_F(SAlignReporterTest, ExtractOptimizableExcludesSkippedStructs)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(
      createMetrics("Optimizable", "file.c", 100, 80, 20, 0));  // savings = 20

  auto skipped = createMetrics("Packed", "file.c", 80, 80, 0, 0);
  skipped.skipped = true;
  metrics.push_back(skipped);

  auto result = alchemy::metrics::reporter::detail::extractOptimizable(metrics);

  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result[0].structName, "Optimizable");
}

TEST_F(SAlignReporterTest, ReportPrintsSkippedPackedStructWarning)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;

  // one optimizable struct
  metrics.push_back(createMetrics("Normal", "file.c", 100, 80, 20, 0));

  // two skipped packed structs
  auto skipped1 = createMetrics("Packed1", "file.c", 80, 80, 0, 0);
  skipped1.skipped = true;
  metrics.push_back(skipped1);

  auto skipped2 = createMetrics("Packed2", "file.c", 64, 64, 0, 0);
  skipped2.skipped = true;
  metrics.push_back(skipped2);

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_NE(output.find("skipped"), std::string::npos)
      << "output should contain 'skipped' for packed structs";
  ASSERT_NE(output.find("2"), std::string::npos)
      << "output should contain the count of skipped structs";
  ASSERT_NE(output.find("#pragma packed"), std::string::npos)
      << "output should mention #pragma packed";
}

TEST_F(SAlignReporterTest, ReportDoesNotPrintSkippedWarningWhenNoneSkipped)
{
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  metrics.push_back(createMetrics("Normal", "file.c", 100, 80, 20, 0));

  ::testing::internal::CaptureStdout();
  alchemy::metrics::reporter::SAlignReporter::report(metrics);
  std::string output = ::testing::internal::GetCapturedStdout();

  ASSERT_EQ(output.find("skipped"), std::string::npos)
      << "output should not contain 'skipped' when no structs are skipped";
}

}  // namespace alchemy::testing
