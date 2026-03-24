// tests/unit/test_salign_invariants.cpp
// unit tests validating mathematical invariants of struct alignment metrics
// Uses hand-calculated layout values to avoid circular verification.
//
// Reference layout (unoptimized): char(1,align=1), double(8,align=8), int(4,align=4)
//   naturalAlignment = 8
//   Layout: char@0(1) + 7pad + double@8(8) + int@16(4) + 4pad = 24 bytes
//   dataSize = 1+8+4 = 13, totalSize = 24, waste = 11
//
// Reference layout (optimal): double(8,align=8), int(4,align=4), char(1,align=1)
//   naturalAlignment = 8
//   Layout: double@0(8) + int@8(4) + char@12(1) + 3pad = 16 bytes
//   dataSize = 8+4+1 = 13, totalSize = 16, waste = 3

// std
#include <cmath>
#include <cstddef>

#include <numeric>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "operation/refactoring/salign_operation.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "utils.hpp"

namespace alchemy::testing {

// hand-calculated constants for unoptimized struct: char, double, int
constexpr std::size_t UnoptDataSize = 13;   // 1 + 8 + 4
constexpr std::size_t UnoptTotalSize = 24;  // char@0 + 7pad + double@8 + int@16 + 4pad
constexpr std::size_t UnoptWaste = 11;      // 24 - 13
constexpr std::size_t UnoptAlignment = 8;

// hand-calculated constants for already-optimal struct: double, int, char
constexpr std::size_t OptDataSize = 13;   // 8 + 4 + 1
constexpr std::size_t OptTotalSize = 16;  // double@0 + int@8 + char@12 + 3pad
constexpr std::size_t OptWaste = 3;       // 16 - 13
constexpr std::size_t OptAlignment = 8;

class SalignMetricsInvariants : public ::testing::Test {
protected:
  void
  SetUp() override
  {
  }
  void
  TearDown() override
  {
  }
};

//==============================================================================
// production method verification (breaks circular dependency)
//==============================================================================
TEST_F(SalignMetricsInvariants, ComputeDataSizeMatchesHandCalculated)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };

  ASSERT_EQ(
      alchemy::parser::artifacts::StructDef::computeDataSize(Fields),
      UnoptDataSize)
      << "computeDataSize should return sum of field naturalSize values (1+8+4=13)";
}

TEST_F(SalignMetricsInvariants, ComputeSizeMatchesHandCalculated)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };

  ASSERT_EQ(
      alchemy::parser::artifacts::StructDef::computeSize(Fields, UnoptAlignment),
      UnoptTotalSize)
      << "computeSize should return 24 for char+double+int with align=8";
}

TEST_F(SalignMetricsInvariants, ComputeSizeMatchesHandCalculatedOptimal)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 14, "double", "a", 8, 8),
      utils::createFieldDef(14, 11, "int", "b", 4, 4),
      utils::createFieldDef(25, 12, "char", "c", 1, 1)
  };

  ASSERT_EQ(
      alchemy::parser::artifacts::StructDef::computeSize(Fields, OptAlignment),
      OptTotalSize)
      << "computeSize should return 16 for double+int+char with align=8";
}

//==============================================================================
// invariant 1: data size conservation
//==============================================================================
TEST_F(SalignMetricsInvariants, ReorderingFieldsDoesNotChangeDataSize)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // data size must be constant across reordering
  const std::size_t OptimizedDataSize =
      analysis.metrics.optimizedSize - analysis.metrics.optimizedWaste;

  ASSERT_EQ(OptimizedDataSize, UnoptDataSize)
      << "reordering cannot add or remove field bytes (data size must be "
         "constant)";

  ASSERT_EQ(analysis.metrics.currentDataSize, UnoptDataSize)
      << "current data size should equal hand-calculated value (13)";

  // verify data size equals sum of field sizes
  const std::size_t FieldSizeSum =
      std::accumulate(structDef.fields.begin(),
                      structDef.fields.end(),
                      std::size_t{0},
                      [](std::size_t sum, const auto& field) {
                        return sum + field.naturalSize;
                      });

  ASSERT_EQ(FieldSizeSum, UnoptDataSize)
      << "sum of field natural sizes should equal hand-calculated data size";
}

//==============================================================================
// invariant 2: size decomposition
//==============================================================================
TEST_F(SalignMetricsInvariants, TotalSizeEqualsDataSizePlusPaddingWaste)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // current layout: total = data + padding
  ASSERT_EQ(analysis.metrics.naturalTotalSize,
            analysis.metrics.currentDataSize +
                analysis.metrics.currentWastedBytes)
      << "current: totalSize must equal dataSize + paddingWaste";

  // verify against hand-calculated values
  ASSERT_EQ(analysis.metrics.naturalTotalSize, UnoptTotalSize);
  ASSERT_EQ(analysis.metrics.currentWastedBytes, UnoptWaste);

  // optimized layout: total = data + padding
  ASSERT_EQ(analysis.metrics.optimizedSize,
            analysis.metrics.currentDataSize + analysis.metrics.optimizedWaste)
      << "optimized: totalSize must equal dataSize + paddingWaste";
}

//==============================================================================
// invariant 3: optimization monotonicity
//==============================================================================
TEST_F(SalignMetricsInvariants,
       OptimizationReducesOrMaintainsSizeNeverIncreases)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // optimization can only reduce or maintain, never increase
  ASSERT_LE(analysis.metrics.optimizedSize, UnoptTotalSize)
      << "optimized size must be <= current size (cannot make things worse)";

  ASSERT_LE(analysis.metrics.optimizedWaste, UnoptWaste)
      << "optimized waste must be <= current waste (cannot make things worse)";

  // optimized should reach the known optimal: 16 bytes
  ASSERT_EQ(analysis.metrics.optimizedSize, OptTotalSize)
      << "optimal reorder of char+double+int with align=8 should be 16 bytes";
}

TEST_F(SalignMetricsInvariants,
       OptimizationOfAlreadyOptimalStructChangesNothing)
{
  // already optimal: double(8), int(4), char(1)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 14, "double", "a", 8, 8),
      utils::createFieldDef(14, 11, "int", "b", 4, 4),
      utils::createFieldDef(25, 12, "char", "c", 1, 1)
  };
  auto structDef = utils::createStructDefExplicit(
      "OptimalStruct", "/test/file.h", Fields, OptAlignment,
      OptDataSize, OptTotalSize, OptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // already optimal - no change possible
  ASSERT_EQ(analysis.metrics.optimizedSize, OptTotalSize)
      << "already optimal struct should have same size after optimization";
  ASSERT_EQ(analysis.metrics.optimizedWaste, OptWaste)
      << "already optimal struct should have same waste after optimization";
  ASSERT_EQ(analysis.metrics.possibleSavings, 0)
      << "already optimal struct should have zero savings";
}

//==============================================================================
// invariant 4: percentage formula consistency
//==============================================================================
TEST_F(SalignMetricsInvariants, WastePercentageMatchesWasteBytesOverTotalSize)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // current waste percentage against hand-calculated values
  const double ExpectedCurrentWastePercent =
      (static_cast<double>(UnoptWaste) /
       static_cast<double>(UnoptTotalSize)) *
      100.0;
  ASSERT_NEAR(
      analysis.metrics.currentWastePercent, ExpectedCurrentWastePercent, 0.01)
      << "current waste % must equal (wasteBytes / totalSize) x 100";

  // optimized waste percentage
  const double ExpectedOptimizedWastePercent =
      (static_cast<double>(analysis.metrics.optimizedWaste) /
       static_cast<double>(analysis.metrics.optimizedSize)) *
      100.0;
  ASSERT_NEAR(analysis.metrics.optimizedWastePercent,
              ExpectedOptimizedWastePercent,
              0.01)
      << "optimized waste % must equal (optimizedWaste / optimizedSize) x 100";

  // percentages must be in valid range
  ASSERT_GE(analysis.metrics.currentWastePercent, 0.0);
  ASSERT_LE(analysis.metrics.currentWastePercent, 100.0);
  ASSERT_GE(analysis.metrics.optimizedWastePercent, 0.0);
  ASSERT_LE(analysis.metrics.optimizedWastePercent, 100.0);
}

//==============================================================================
// invariant 5: savings consistency
//==============================================================================
TEST_F(SalignMetricsInvariants,
       SavingsPercentageMatchesBytesSavedOverCurrentSize)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // savings bytes = current - optimized (using hand-calculated current)
  const std::size_t ExpectedSavings = UnoptTotalSize - OptTotalSize;  // 24 - 16 = 8
  ASSERT_EQ(analysis.metrics.possibleSavings, ExpectedSavings)
      << "savings bytes must equal currentSize - optimizedSize (24-16=8)";

  // savings percentage against hand-calculated values
  const double ExpectedSavingsPercent =
      (static_cast<double>(ExpectedSavings) /
       static_cast<double>(UnoptTotalSize)) *
      100.0;
  ASSERT_NEAR(analysis.metrics.savingsPercent, ExpectedSavingsPercent, 0.01)
      << "savings % must equal (bytesSaved / currentTotal) x 100";

  // savings percentage must be in valid range
  ASSERT_GE(analysis.metrics.savingsPercent, 0.0);
  ASSERT_LE(analysis.metrics.savingsPercent, 100.0);
}

//==============================================================================
// invariant 6: cache metrics bounds
//==============================================================================
TEST_F(SalignMetricsInvariants, CacheWasteMustBeLessThanCacheLineSize)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // cache waste is unused bytes per cache line - must be < 64
  ASSERT_LT(analysis.metrics.currentCacheWaste, 64)
      << "current cache waste must be < 64 bytes (cache line size)";
  ASSERT_LT(analysis.metrics.optimizedCacheWaste, 64)
      << "optimized cache waste must be < 64 bytes (cache line size)";
}

TEST_F(SalignMetricsInvariants, CacheUtilizationPercentageIsWithinValidRange)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // cache utilization must be [0%, 100%]
  ASSERT_GE(analysis.metrics.currentCacheUtil, 0.0);
  ASSERT_LE(analysis.metrics.currentCacheUtil, 100.0);
  ASSERT_GE(analysis.metrics.optimizedCacheUtil, 0.0);
  ASSERT_LE(analysis.metrics.optimizedCacheUtil, 100.0);
}

//==============================================================================
// invariant 7: splc formula
//==============================================================================
TEST_F(SalignMetricsInvariants,
       StructsPerCacheLineMatchesFloorOfSixtyFourDividedBySize)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 11, "int", "c", 4, 4)
  };
  auto structDef = utils::createStructDefExplicit(
      "TestStruct", "/test/file.h", Fields, UnoptAlignment,
      UnoptDataSize, UnoptTotalSize, UnoptWaste);

  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  auto analysis = Op.analyzeStruct(structDef);

  // current spcl = floor(64 / currentSize) -- using hand-calculated total
  const double ExpectedCurrentSpcl =
      std::floor(64.0 / static_cast<double>(UnoptTotalSize));  // floor(64/24) = 2
  ASSERT_EQ(analysis.metrics.currentSpclScore, ExpectedCurrentSpcl)
      << "current splc must equal floor(64 / currentSize)";

  // optimized splc = floor(64 / optimizedSize)
  const double ExpectedOptimizedSpcl =
      std::floor(64.0 / static_cast<double>(OptTotalSize));  // floor(64/16) = 4
  ASSERT_EQ(analysis.metrics.optimizedSpclScore, ExpectedOptimizedSpcl)
      << "optimized splc must equal floor(64 / optimizedSize)";

  // splc must be non-negative
  ASSERT_GE(analysis.metrics.currentSpclScore, 0.0);
  ASSERT_GE(analysis.metrics.optimizedSpclScore, 0.0);
}

}  // namespace alchemy::testing
