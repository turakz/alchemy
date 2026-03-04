// tests/unit/test_transmute.cpp

// std
#include <filesystem>
#include <string>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "app/core/core.hpp"
#include "operation/operation_base.hpp"
#include "transmute/transmute.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class TransmuteTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temp directory for test files
    tempDir = std::filesystem::temp_directory_path() / "alchemy_transmute_test";
    std::filesystem::create_directories(tempDir);
  }

  void
  TearDown() override
  {
    // cleanup temp directory
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
};

// test transmute::applyRecipes with single recipe
TEST_F(TransmuteTest, AppliesSingleRecipe)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "test.txt", OriginalContent);

  // create recipe to replace "line 2" at byte offset 7 (after "line 1\n")
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 7, 6, "REPLACED LINE 2"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::transmute should succeed";

  auto transmutationResult = result.value();
  ASSERT_EQ(transmutationResult.file, TestFile);
  ASSERT_EQ(transmutationResult.recipesApplied, 1);

  // verify file content was modified correctly
  const std::string ExpectedContent = "line 1\nREPLACED LINE 2\nline 3\n";

  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, ExpectedContent);
}

// test transmute::applyRecipes with multiple recipes
TEST_F(TransmuteTest, AppliesMultipleRecipes)
{
  const std::string OriginalContent =
      "line 1\nline 2\nline 3\nline 4\nline 5\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "multi.txt", OriginalContent);

  // create multiple recipes (sorted by byteOffset - required precondition)
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(
      utils::createRecipe(TestFile, 0, 6, "REPLACED 1"));  // line 1 at offset 0
  recipes.push_back(
      utils::createRecipe(TestFile, 7, 6, "REPLACED 2"));  // line 2 at offset 7
  recipes.push_back(utils::createRecipe(
      TestFile, 21, 6, "REPLACED 4"));  // line 4 at offset 21

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid());

  auto transmutationResult = result.value();
  ASSERT_EQ(transmutationResult.recipesApplied, 3);

  // verify all replacements were applied
  const std::string ExpectedContent =
      "REPLACED 1\nREPLACED 2\nline 3\nREPLACED 4\nline 5\n";

  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, ExpectedContent);
}

// test transmute::applyRecipes handles nonexistent file
TEST_F(TransmuteTest, HandlesNonexistentFile)
{
  const std::filesystem::path NonexistentFile = tempDir / "does_not_exist.txt";

  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(NonexistentFile, 0, 5, "replacement"));

  auto result = alchemy::transmute::applyRecipes(
      NonexistentFile, recipes, std::filesystem::path{}, false);

  ASSERT_FALSE(result.valid())
      << "alchemy::testing::unit::transmute should fail for nonexistent file";
}

// test transmute::applyRecipes handles out of bounds byte offset
TEST_F(TransmuteTest, HandlesOutOfBoundsByteOffset)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "bounds.txt", OriginalContent);

  // try to replace at byte offset 100 (out of bounds - file is only 21 bytes)
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 100, 5, "out of bounds"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_FALSE(result.valid()) << "alchemy::testing::unit::transmute should "
                                  "fail for out of bounds byte offset";
}

// test transmute::applyRecipes with empty recipes
TEST_F(TransmuteTest, HandlesEmptyRecipes)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "empty.txt", OriginalContent);

  const std::vector<alchemy::operation::Recipe> Recipes;  // empty

  auto result = alchemy::transmute::applyRecipes(
      TestFile, Recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::transmute should succeed with empty recipes";

  auto transmutationResult = result.value();
  ASSERT_EQ(transmutationResult.recipesApplied, 0);

  // file should remain unchanged
  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, OriginalContent);
}

// test transmute::applyRecipes with struct field reordering (realistic
// scenario)
TEST_F(TransmuteTest, ReordersStructFields)
{
  const std::string OriginalContent =
      "struct UnoptimizedStruct {\n    char smallField;\n    double "
      "bigField;\n    int mediumField;\n};\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "struct.h", OriginalContent);

  // create recipes to reorder fields: double, int, char
  // char smallField line at offset 27, length 21
  // double bigField line at offset 48, length 21
  // int mediumField line at offset 69, length 21

  // verify offsets match expected content
  ASSERT_EQ(OriginalContent.substr(27, 21), "    char smallField;\n");
  ASSERT_EQ(OriginalContent.substr(48, 21), "    double bigField;\n");
  ASSERT_EQ(OriginalContent.substr(69, 21), "    int mediumField;\n");

  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(
      utils::createRecipe(TestFile, 27, 21, "    double bigField;\n"));
  recipes.push_back(
      utils::createRecipe(TestFile, 48, 21, "    int mediumField;\n"));
  recipes.push_back(
      utils::createRecipe(TestFile, 69, 21, "    char smallField;\n"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid());

  auto transmutationResult = result.value();
  ASSERT_EQ(transmutationResult.recipesApplied, 3);

  // verify struct was reordered
  const std::string ExpectedContent =
      "struct UnoptimizedStruct {\n    double bigField;\n    int "
      "mediumField;\n    char smallField;\n};\n";

  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, ExpectedContent);
}

// test edge case: empty file with recipes should fail gracefully
TEST_F(TransmuteTest, HandlesEmptyFile)
{
  const std::string OriginalContent;

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "empty_file.txt", OriginalContent);

  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 0, 5, "line 1"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  // should fail because byte range [0, 5) is out of bounds for empty file
  ASSERT_FALSE(result.valid()) << "alchemy::testing::unit::transmute should "
                                  "fail for recipes on empty file";
}

// test edge case: byte offset 0 with valid length should succeed
TEST_F(TransmuteTest, HandlesByteOffsetZero)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "offset_zero.txt", OriginalContent);

  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 0, 6, "FIRST LINE"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::transmute should succeed for byte offset 0";

  const std::string ExpectedContent = "FIRST LINE\nline 2\nline 3\n";
  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, ExpectedContent);
}

// test boundary: byteOffset + byteLength exactly equals file size
TEST_F(TransmuteTest, HandlesBoundaryExactFileSize)
{
  const std::string OriginalContent = "0123456789";  // exactly 10 bytes

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "boundary.txt", OriginalContent);

  // replace last 5 bytes (offset 5, length 5) - should succeed
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 5, 5, "ABCDE"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::transmute should succeed when byteOffset + "
         "byteLength == fileSize";

  const std::string ExpectedContent = "01234ABCDE";
  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, ExpectedContent);
}

// test boundary: byteOffset + byteLength exceeds file size by 1
TEST_F(TransmuteTest, HandlesBoundaryExceedsFileSizeByOne)
{
  const std::string OriginalContent = "0123456789";  // exactly 10 bytes

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "boundary_exceed.txt", OriginalContent);

  // try to replace 6 bytes starting at offset 5 (would need 11 bytes total)
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 5, 6, "ABCDEF"));

  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, false);

  ASSERT_FALSE(result.valid())
      << "alchemy::testing::unit::transmute should fail when byteOffset + "
         "byteLength > fileSize";
}

// test dry run mode doesn't modify files
TEST_F(TransmuteTest, RefactorDryRunDoesNotModifyFile)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "dryrun.txt", OriginalContent);

  // create recipe to replace "line 2"
  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 7, 6, "REPLACED"));

  // apply with dry run enabled
  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{}, true);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::dry run should succeed";

  // verify file was NOT modified
  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_EQ(ActualContent, OriginalContent)
      << "alchemy::testing::unit::dry run should not modify file";
}

// test config fields are accessible without breaking transmutation
TEST_F(TransmuteTest, ConfigFieldsAccessibleButNotUsed)
{
  const std::string OriginalContent = "line 1\nline 2\nline 3\n";

  const std::filesystem::path TestFile =
      utils::createTestFile(tempDir, "config_test.txt", OriginalContent);

  std::vector<alchemy::operation::Recipe> recipes;
  recipes.push_back(utils::createRecipe(TestFile, 7, 6, "REPLACED"));

  // apply with buildDir parameter (other fields not needed by transmute)
  auto result = alchemy::transmute::applyRecipes(
      TestFile, recipes, std::filesystem::path{"/build"}, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::config with various fields should not "
         "break transmutation";

  // verify transmutation succeeded
  const std::string ActualContent = utils::readFile(TestFile);
  ASSERT_NE(ActualContent, OriginalContent)
      << "alchemy::testing::unit::file should be modified";
}

}  // namespace alchemy::testing
