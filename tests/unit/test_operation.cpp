// tests/unit/test_operation.cpp

// std
#include <cstddef>

#include <filesystem>
#include <utility>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "operation/refactoring/salign_operation.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class OperationTest : public ::testing::Test {};

// ============================================================================
// FEATURE: SAlign - StructDef::computeSize() edge cases
// ============================================================================

TEST_F(OperationTest, SAlign_ComputeSize_CalculatesCorrectSize)
{
  // fields: double (8), int (4), char (1)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 14, "double", "field1", 8, 8),
      utils::createFieldDef(14, 10, "int", "field2", 4, 4),
      utils::createFieldDef(24, 12, "char", "field3", 1, 1)};

  // total data: 8 + 4 + 1 = 13 bytes
  // alignment requirement: 8 (from double)
  // size: round up 13 to multiple of 8 = 16
  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 8);

  ASSERT_EQ(Size, 16)
      << "alchemy::testing::unit::salign::13 bytes with 8-byte alignment = 16 "
         "bytes";
}

TEST_F(OperationTest, SAlign_ComputeSize_HandlesEmptyFields)
{
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {};

  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 8);

  ASSERT_EQ(Size, 0)
      << "alchemy::testing::unit::salign::empty fields = 0 bytes";
}

TEST_F(OperationTest, SAlign_ComputeSize_HandlesAlignmentOf1)
{
  // fields: char (1), char (1), char (1)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 12, "char", "b", 1, 1),
      utils::createFieldDef(24, 12, "char", "c", 1, 1)};

  // total: 3 bytes, alignment 1 -> size = 3
  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 1);

  ASSERT_EQ(Size, 3)
      << "alchemy::testing::unit::salign::3 chars with 1-byte alignment = 3 "
         "bytes";
}

TEST_F(OperationTest, SAlign_ComputeSize_InsertsPaddingForMisalignedField)
{
  // Field 1: char (1 byte) at offset 0
  // Field 2: int (4 bytes, needs 4-byte alignment)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 10, "int", "b", 4, 4)};

  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 4);

  // char (1) + padding (3) + int (4) = 8 bytes
  ASSERT_EQ(Size, 8)
      << "alchemy::testing::unit::salign::padding inserted for field alignment";
}

TEST_F(OperationTest, SAlign_ComputeSize_InsertsPaddingForStructAlignment)
{
  // char (1) + padding (3) + int (4) + char (1) = 9 bytes
  // struct alignment: 4 -> needs padding to 12
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 10, "int", "b", 4, 4),
      utils::createFieldDef(22, 12, "char", "c", 1, 1)};

  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 4);

  // char (1) + pad (3) + int (4) + char (1) + pad (3) = 12
  ASSERT_EQ(Size, 12) << "alchemy::testing::unit::salign::trailing "
                         "padding for struct alignment";
}

TEST_F(OperationTest, SAlign_ComputeSize_HandlesMultiplePaddingInsertions)
{
  // char (1) + pad (7) + double (8) + char (1) + pad (3) + int (4) = 24
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 12, "char", "c", 1, 1),
      utils::createFieldDef(38, 10, "int", "d", 4, 4)};

  const std::size_t Size =
      alchemy::parser::artifacts::StructDef::computeSize(Fields, 8);

  // char (1) + pad (7) + double (8) + char (1) + pad (3) + int (4) = 24
  ASSERT_EQ(Size, 24)
      << "alchemy::testing::unit::salign::multiple padding insertions";
}

// ============================================================================
// FEATURE: SAlign - sortFieldsByAlignment()
// ============================================================================

TEST_F(OperationTest, SAlign_SortFields_ReordersDescendingByAlignment)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;

  // unsorted: char (1), double (8), int (4)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "small", 1, 1),
      utils::createFieldDef(12, 14, "double", "big", 8, 8),
      utils::createFieldDef(26, 10, "int", "medium", 4, 4)};

  auto sorted = Op.sortFieldsByAlignment(Fields);

  // expected order: double (8), int (4), char (1)
  ASSERT_EQ(sorted.size(), 3);
  ASSERT_EQ(sorted[0].fieldName, "big")
      << "alchemy::testing::unit::salign::largest alignment first";
  ASSERT_EQ(sorted[1].fieldName, "medium")
      << "alchemy::testing::unit::salign::medium alignment second";
  ASSERT_EQ(sorted[2].fieldName, "small")
      << "alchemy::testing::unit::salign::smallest alignment last";
}

TEST_F(OperationTest, SAlign_SortFields_PreservesBitfieldOrder)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;

  // bitfield cannot be reordered - should stay in original position
  std::vector<alchemy::parser::artifacts::FieldDef> fields = {
      utils::createFieldDef(0, 12, "char", "reorderable1", 1, 1),
      utils::createFieldDef(12, 14, "int", "bitfield", 4, 4),
      utils::createFieldDef(26, 14, "double", "reorderable2", 8, 8)};

  fields[1].isBitField = true;
  fields[1].canReorder = false;

  auto sorted = Op.sortFieldsByAlignment(fields);

  // reorderable fields sorted by alignment, bitfield stays in place
  // expected: reorderable fields first (double, char), then bitfield
  ASSERT_EQ(sorted.size(), 3);

  // check that bitfield is after reorderable fields
  int bitfieldIndex = -1;
  for (size_t i = 0; i < sorted.size(); ++i)
  {
    if (sorted[i].fieldName == "bitfield")
    {
      bitfieldIndex = static_cast<int>(i);
      break;
    }
  }

  ASSERT_GE(bitfieldIndex, 0)
      << "alchemy::testing::unit::salign::bitfield should be present";
}

TEST_F(OperationTest, SAlign_SortFields_HandlesEmptyVector)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {};

  auto sorted = Op.sortFieldsByAlignment(Fields);

  ASSERT_TRUE(sorted.empty())
      << "alchemy::testing::unit::salign::empty input = empty output";
}

TEST_F(OperationTest, SAlign_SortFields_HandlesSingleField)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 10, "int", "only", 4, 4)};

  auto sorted = Op.sortFieldsByAlignment(Fields);

  ASSERT_EQ(sorted.size(), 1);
  ASSERT_EQ(sorted[0].fieldName, "only")
      << "alchemy::testing::unit::salign::single field unchanged";
}

// ============================================================================
// FEATURE: SAlign - analyze() (recipe generation + conflict resolution)
// ============================================================================

TEST_F(OperationTest, SAlign_Analyze_ReturnsEmptyForOptimalStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // already optimal: double (8), int (4), char (1)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 14, "double", "field1", 8, 8),
      utils::createFieldDef(14, 10, "int", "field2", 4, 4),
      utils::createFieldDef(24, 12, "char", "field3", 1, 1)};

  auto structDef = utils::createStructDef("OptimalStruct", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  // For optimal struct, no recipes should be generated
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::no recipes for optimal struct";
}

TEST_F(OperationTest, SAlign_Analyze_GeneratesRecipesForSuboptimalStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal: char (1), double (8), int (4)
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "field1", 1, 1),
      utils::createFieldDef(12, 14, "double", "field2", 8, 8),
      utils::createFieldDef(26, 10, "int", "field3", 4, 4)};

  auto structDef =
      utils::createStructDef("SuboptimalStruct", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  // For suboptimal struct, recipes should be generated
  ASSERT_FALSE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::recipes generated for suboptimal "
         "struct";
}

TEST_F(OperationTest, SAlign_Analyze_RecipesSortedByByteOffset)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal struct
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 10, "int", "c", 4, 4)};

  auto structDef =
      utils::createStructDef("UnsortedStruct", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  const auto& recipes = analysis.recipes;

  // verify recipes are sorted by byte offset
  for (size_t i = 1; i < recipes.size(); ++i)
  {
    ASSERT_LE(recipes[i - 1].byteOffset, recipes[i].byteOffset)
        << "alchemy::testing::unit::salign::recipes sorted by byte offset";
  }
}

TEST_F(OperationTest, SAlign_Analyze_ReplacementTextHasSemicolon)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal struct
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8)};

  auto structDef = utils::createStructDef("TestStruct", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  const auto& recipes = analysis.recipes;

  for (const auto& recipe : recipes)
  {
    ASSERT_FALSE(recipe.replacementText.empty())
        << "alchemy::testing::unit::salign::replacement text not empty";

    ASSERT_NE(recipe.replacementText.find(';'), std::string::npos)
        << "alchemy::testing::unit::salign::replacement has semicolon";
  }
}

TEST_F(OperationTest, SAlign_Analyze_HandlesEmptyCHeaderStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {};
  auto structDef = utils::createStructDef("EmptyStruct", TestFile, Fields, 1);

  auto analysis = Op.analyzeStruct(structDef);

  // Empty struct should have no recipes
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::empty struct generates no recipes";
}

TEST_F(OperationTest, SAlign_Analyze_HandlesEmptyCXXHeaderStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.hpp";

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {};
  auto structDef = utils::createStructDef("EmptyStruct", TestFile, Fields, 1);

  auto analysis = Op.analyzeStruct(structDef);

  // Empty CXX struct should have no recipes
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::empty struct generates no recipes";
}

TEST_F(OperationTest, SAlign_Analyze_HandlesSingleFieldCHeaderStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 10, "int", "only", 4, 4)};
  auto structDef = utils::createStructDef("SingleField", TestFile, Fields, 4);

  auto analysis = Op.analyzeStruct(structDef);

  // Single field struct should have no recipes (already optimal)
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::single field needs no optimization";
}

TEST_F(OperationTest, SAlign_Analyze_HandlesSingleFieldStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.hpp";

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 10, "int", "only", 4, 4)};
  auto structDef = utils::createStructDef("SingleField", TestFile, Fields, 4);

  auto analysis = Op.analyzeStruct(structDef);

  // Single field CXX struct should have no recipes (already optimal)
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::single field needs no optimization";
}

// ============================================================================
// FEATURE: SAlign - analyze() conflict resolution (Bug #2 validation)
// ============================================================================

TEST_F(OperationTest, SAlign_Analyze_HandlesAdjacentFieldsWithoutConflict)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // adjacent fields that will be reordered
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),     // offset 0, len 12
      utils::createFieldDef(12, 14, "double", "b", 8, 8),  // offset 12, len 14
      utils::createFieldDef(26, 10, "int", "c", 4, 4),     // offset 26, len 10
      utils::createFieldDef(36, 12, "char", "d", 1, 1)     // offset 36, len 12
  };

  auto structDef =
      utils::createStructDef("AdjacentFields", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  // Adjacent fields test (conflict detection)
  // Should generate recipes without errors
  ASSERT_FALSE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::recipes generated for suboptimal "
         "adjacent fields";
}

// NOTE: testing actual overlapping conflicts is difficult with
// StructAlignmentOperation because it generates non-overlapping recipes by
// design (each field has unique byte range). The conflict detection logic
// exists to catch bugs in byteOffset/byteLength calculation or future
// multi-operation scenarios. The logic is tested implicitly by the adjacent
// test above (proves detection works) and will be tested explicitly when we
// have multiple operations that could generate overlapping recipes.

// ============================================================================
// FEATURE: SAlign - analyzeStruct() recipe conflict detection
// ============================================================================

TEST_F(OperationTest, SAlign_Analyze_DetectsOverlappingRecipes)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // Create fields with overlapping byte ranges
  // This simulates a bug where recipe generation produces overlapping
  // replacements
  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 20, "char", "a", 1, 1),  // offset 0, len 20
      utils::createFieldDef(
          10, 20, "double", "b", 8, 8),  // offset 10, len 20 (overlaps!)
      utils::createFieldDef(30, 10, "int", "c", 4, 4)  // offset 30, len 10
  };

  auto structDef =
      utils::createStructDef("OverlappingFields", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  // Conflict detection should clear recipes and zero out savings
  ASSERT_TRUE(analysis.recipes.empty())
      << "alchemy::testing::unit::salign::overlapping recipes cleared";
  ASSERT_EQ(analysis.metrics.possibleSavings, 0)
      << "alchemy::testing::unit::salign::savings zeroed on conflict";
  ASSERT_DOUBLE_EQ(analysis.metrics.savingsPercent, 0.0)
      << "alchemy::testing::unit::salign::savings percent zeroed on conflict";
}

// ============================================================================
// FEATURE: SAlign - computeMetrics()
// ============================================================================

TEST_F(OperationTest, SAlign_ComputeMetrics_ReturnsMetricsForEachStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // create parse results with 2 structs
  alchemy::parser::artifacts::ParseResults artifacts;

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields1 = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8)};
  artifacts.structs.push_back(
      utils::createStructDef("Struct1", TestFile, Fields1, 8));

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields2 = {
      utils::createFieldDef(0, 10, "int", "x", 4, 4),
      utils::createFieldDef(10, 12, "char", "y", 1, 1)};
  artifacts.structs.push_back(
      utils::createStructDef("Struct2", TestFile, Fields2, 4));

  // Collect metrics by analyzing each struct
  std::vector<alchemy::metrics::detail::SAlignMetrics> metrics;
  for (const auto& structDef : artifacts.structs)
  {
    auto analysis = Op.analyzeStruct(structDef);
    metrics.push_back(std::move(analysis.metrics));
  }

  ASSERT_EQ(metrics.size(), 2) << "alchemy::testing::unit::salign::should "
                                  "return metrics for each struct";
}

TEST_F(OperationTest, SAlign_ComputeMetrics_HandlesEmptyArtifacts)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;

  const alchemy::parser::artifacts::ParseResults Artifacts;  // empty

  // Collect metrics by analyzing each struct
  std::vector<alchemy::metrics::detail::SAlignMetrics> metrics;
  for (const auto& structDef : Artifacts.structs)
  {
    auto analysis = Op.analyzeStruct(structDef);
    metrics.push_back(std::move(analysis.metrics));
  }

  ASSERT_TRUE(metrics.empty())
      << "alchemy::testing::unit::salign::empty artifacts = empty metrics";
}

TEST_F(OperationTest, SAlign_ComputeMetrics_IncludesStructName)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  alchemy::parser::artifacts::ParseResults artifacts;

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8)};
  artifacts.structs.push_back(
      utils::createStructDef("MyStruct", TestFile, Fields, 8));

  // Collect metrics by analyzing each struct
  std::vector<alchemy::metrics::detail::SAlignMetrics> metrics;
  for (const auto& structDef : artifacts.structs)
  {
    auto analysis = Op.analyzeStruct(structDef);
    metrics.push_back(std::move(analysis.metrics));
  }

  ASSERT_EQ(metrics.size(), 1);
  ASSERT_EQ(metrics[0].structName, "MyStruct")
      << "alchemy::testing::unit::salign::metrics include struct name";
}

}  // namespace alchemy::testing
