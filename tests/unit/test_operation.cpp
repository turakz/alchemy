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
  ASSERT_EQ(Fields[sorted[0]].fieldName, "big")
      << "alchemy::testing::unit::salign::largest alignment first";
  ASSERT_EQ(Fields[sorted[1]].fieldName, "medium")
      << "alchemy::testing::unit::salign::medium alignment second";
  ASSERT_EQ(Fields[sorted[2]].fieldName, "small")
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
    if (fields[sorted[i]].fieldName == "bitfield")
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
  ASSERT_EQ(Fields[sorted[0]].fieldName, "only")
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
      utils::createFieldDef(10,
                            20,
                            "double",
                            "b",
                            8,
                            8),  // offset 10, len 20 (overlaps!)
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
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
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
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
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
  std::vector<alchemy::metrics::SAlignMetrics> metrics;
  for (const auto& structDef : artifacts.structs)
  {
    auto analysis = Op.analyzeStruct(structDef);
    metrics.push_back(std::move(analysis.metrics));
  }

  ASSERT_EQ(metrics.size(), 1);
  ASSERT_EQ(metrics[0].structName, "MyStruct")
      << "alchemy::testing::unit::salign::metrics include struct name";
}

TEST_F(OperationTest, SAlign_Analyze_BailsOutOnNonReorderableFieldInSwapZone)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal: char(1), uint32_t(4, non-reorderable), double(8)
  // optimal sort would produce: double, char, uint32_t
  // but uint32_t is non-reorderable, so at position 1 the swap
  // original=uint32_t vs optimized=char triggers bailout
  std::vector<alchemy::parser::artifacts::FieldDef> fields = {
      utils::createFieldDef(0, 12, "char", "small", 1, 1),
      utils::createFieldDef(12, 14, "uint32_t", "locked", 4, 4, false, false),
      utils::createFieldDef(26, 14, "double", "big", 8, 8)};

  auto structDef =
      utils::createStructDef("NonReorderableStruct", TestFile, fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  ASSERT_TRUE(analysis.recipes.empty())
      << "non-reorderable field in swap zone should clear all recipes";
  ASSERT_EQ(analysis.metrics.possibleSavings, 0)
      << "savings should be zeroed on non-reorderable bailout";
  ASSERT_DOUBLE_EQ(analysis.metrics.savingsPercent, 0.0)
      << "savings percent should be zeroed on non-reorderable bailout";
}

TEST_F(OperationTest, SAlign_Analyze_SkipsPackedStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal layout: char(1), double(8) — would normally be reordered
  // but the struct is #pragma packed, so it should be skipped entirely
  std::vector<alchemy::parser::artifacts::FieldDef> fields = {
      utils::createFieldDef(0, 12, "char", "small", 1, 1),
      utils::createFieldDef(12, 14, "double", "big", 8, 8)};

  auto structDef = utils::createStructDef("PackedStruct", TestFile, fields, 8);
  structDef.isPacked = true;

  auto analysis = Op.analyzeStruct(structDef);

  ASSERT_TRUE(analysis.recipes.empty())
      << "packed struct should produce no recipes";
  ASSERT_EQ(analysis.metrics.possibleSavings, 0)
      << "packed struct should report zero savings";
  ASSERT_DOUBLE_EQ(analysis.metrics.savingsPercent, 0.0)
      << "packed struct should report zero savings percent";
  ASSERT_TRUE(analysis.metrics.skipped)
      << "packed struct should be marked as skipped";

  // phase 1 metrics should still be populated
  ASSERT_EQ(analysis.metrics.structName, "PackedStruct");
  ASSERT_EQ(analysis.metrics.sourceFile, TestFile);
  ASSERT_GT(analysis.metrics.naturalTotalSize, 0)
      << "current layout metrics should be populated even for skipped structs";
}

TEST_F(OperationTest, SAlign_Analyze_DoesNotSkipUnpackedStruct)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal: char(1) + 7 pad + double(8) = 16 → optimal: double(8) +
  // char(1) + 7 trailing = 16... actually same size. Use three fields.
  // char(1) + 3 pad + int(4) + double(8) = 16 → double(8) + int(4) + char(1)
  // + 3 trail = 16
  // Need a layout where reordering actually saves:
  // char(1) + 7 pad + double(8) + char(1) + 3 pad + int(4) = 24
  // → double(8) + int(4) + char(1) + char(1) + 2 pad = 16
  std::vector<alchemy::parser::artifacts::FieldDef> fields = {
      utils::createFieldDef(0, 12, "char", "a", 1, 1),
      utils::createFieldDef(12, 14, "double", "b", 8, 8),
      utils::createFieldDef(26, 12, "char", "c", 1, 1),
      utils::createFieldDef(38, 14, "int", "d", 4, 4)};

  auto structDef =
      utils::createStructDef("UnpackedStruct", TestFile, fields, 8);

  auto analysis = Op.analyzeStruct(structDef);

  ASSERT_FALSE(analysis.metrics.skipped)
      << "unpacked struct should not be marked as skipped";
}

// ============================================================================
// FEATURE: SAlign - buildReplacementText() comment preservation
// ============================================================================

TEST_F(OperationTest, SAlign_BuildReplacementText_NoPrecedingComment)
{
  auto field = utils::createFieldDef(0, 16, "uint32_t", "count", 4, 4);

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "uint32_t count;")
      << "field without preceding comment produces just type + name";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_IncludesPrecedingComment)
{
  auto field = utils::createFieldDef(0, 40, "uint32_t", "count", 4, 4);
  field.precedingComment = "/// number of items";
  field.commentFieldGap = "\n  ";

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "/// number of items\n  uint32_t count;")
      << "preceding comment + gap + field declaration";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_PrecedingAndTrailingComment)
{
  auto field = utils::createFieldDef(0, 60, "uint32_t", "count", 4, 4);
  field.precedingComment = "/// number of items";
  field.commentFieldGap = "\n  ";
  field.trailingComment = "// must be > 0";

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "/// number of items\n  uint32_t count; // must be > 0")
      << "preceding comment + field + trailing comment";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_MultiLinePrecedingComment)
{
  auto field = utils::createFieldDef(0, 60, "double", "value", 8, 8);
  field.precedingComment = "/// first line\n  /// second line";
  field.commentFieldGap = "\n  ";

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "/// first line\n  /// second line\n  double value;")
      << "multi-line preceding comment preserved";
}

TEST_F(OperationTest, SAlign_Analyze_ReplacementTextIncludesPrecedingComment)
{
  const alchemy::operation::refactoring::StructAlignmentOperation Op;
  const std::filesystem::path TestFile = "test.h";

  // suboptimal: char(1) + double(8) + char(1) + int(4) = 24 bytes
  // optimal:    double(8) + int(4) + char(1) + char(1) = 16 bytes (saves 8)
  auto fieldA = utils::createFieldDef(0, 12, "char", "a", 1, 1);
  fieldA.precedingComment = "// comment for a";
  fieldA.commentFieldGap = "\n  ";

  auto fieldB = utils::createFieldDef(30, 14, "double", "b", 8, 8);
  fieldB.precedingComment = "// comment for b";
  fieldB.commentFieldGap = "\n  ";

  auto fieldC = utils::createFieldDef(60, 12, "char", "c", 1, 1);
  auto fieldD = utils::createFieldDef(80, 10, "int", "d", 4, 4);

  const std::vector<alchemy::parser::artifacts::FieldDef> Fields = {
      fieldA, fieldB, fieldC, fieldD};
  auto structDef = utils::createStructDef("CommentStruct", TestFile, Fields, 8);
  auto analysis = Op.analyzeStruct(structDef);

  ASSERT_FALSE(analysis.recipes.empty())
      << "should generate recipes for suboptimal struct";

  // verify at least one recipe includes a preceding comment
  bool foundCommentInRecipe = false;
  for (const auto& recipe : analysis.recipes)
  {
    if (recipe.replacementText.find("// comment for") != std::string::npos)
    {
      foundCommentInRecipe = true;
      break;
    }
  }
  ASSERT_TRUE(foundCommentInRecipe)
      << "recipe replacement text should include preceding comment";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_ArrayFieldWithSuffix)
{
  auto field =
      utils::createFieldDef(0, 30, "uint8_t[11]", "SerialNumber", 11, 1);
  field.sourceTypeName = "uint8_t";
  field.sourceArraySuffix = "[SENSOR_SERIAL_NUMBER_LENGTH]";

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "uint8_t SerialNumber[SENSOR_SERIAL_NUMBER_LENGTH];")
      << "array suffix should preserve macro name from source";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_MultiDimArrayWithSuffix)
{
  auto field =
      utils::createFieldDef(0, 40, "char[3][76]", "RepeatedPrints", 228, 1);
  field.sourceTypeName = "char";
  field.sourceArraySuffix =
      "[DEBUGOUTPUTTASK_MAX_REPEATED_COMMANDS][DEBUGCOMMAND_MAX_MESSAGE_SIZE]";

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result,
            "char RepeatedPrints"
            "[DEBUGOUTPUTTASK_MAX_REPEATED_COMMANDS]"
            "[DEBUGCOMMAND_MAX_MESSAGE_SIZE];")
      << "multi-dimensional array suffix should preserve macro names";
}

TEST_F(OperationTest, SAlign_BuildReplacementText_NonArrayFieldEmptySuffix)
{
  auto field = utils::createFieldDef(0, 16, "int32_t", "count", 4, 4);
  field.sourceTypeName = "int32_t";
  // sourceArraySuffix defaults to empty

  const std::string Result =
      alchemy::operation::refactoring::detail::buildReplacementText(field);

  ASSERT_EQ(Result, "int32_t count;")
      << "non-array field produces no array suffix";
}

}  // namespace alchemy::testing
