// tests/unit/test_pipeline_transmute.cpp

// std
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "operation/operation_base.hpp"
#include "pipeline/pipeline.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class PipelineTransmuteTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temp directory for tests
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_transmute_test");
  }

  void
  TearDown() override
  {
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
};

// ============================================================================
// FEATURE: validateTransmute
// ============================================================================

TEST_F(PipelineTransmuteTest, ValidateTransmutePassesForEmptyRecipes)
{
  const std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      EmptyRecipes;

  auto result = alchemy::pipeline::validateTransmute(EmptyRecipes);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::empty recipes should pass validation";
}

TEST_F(PipelineTransmuteTest, ValidateTransmutePassesForValidFiles)
{
  auto file1 = utils::createTestFile(tempDir, "file1.h", "// file1");
  auto file2 = utils::createTestFile(tempDir, "file2.h", "// file2");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[file1.string()] = {
      utils::createRecipe(file1, 0, 10, "// refactored")};
  recipes[file2.string()] = {
      utils::createRecipe(file2, 0, 10, "// refactored")};

  auto result = alchemy::pipeline::validateTransmute(recipes);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::valid writable files should pass validation";
}

TEST_F(PipelineTransmuteTest, ValidateTransmuteFailsForNonexistentFile)
{
  auto nonexistentFile = tempDir / "missing.h";

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[nonexistentFile.string()] = {
      utils::createRecipe(nonexistentFile, 0, 10, "// refactored")};

  auto result = alchemy::pipeline::validateTransmute(recipes);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::nonexistent file should fail validation";
}

TEST_F(PipelineTransmuteTest, ValidateTransmuteFailsForDirectory)
{
  auto directory = tempDir / "subdir";
  std::filesystem::create_directory(directory);

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[directory.string()] = {
      utils::createRecipe(directory, 0, 10, "// code")};

  auto result = alchemy::pipeline::validateTransmute(recipes);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::directory should fail validation";
}

TEST_F(PipelineTransmuteTest, ValidateTransmuteFailsForReadOnlyFile)
{
  auto readonlyFile = utils::createTestFile(tempDir, "readonly.h", "// code");

  // make file read-only
  std::filesystem::permissions(readonlyFile,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace);

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[readonlyFile.string()] = {
      utils::createRecipe(readonlyFile, 0, 10, "// refactored")};

  auto result = alchemy::pipeline::validateTransmute(recipes);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::read-only file should fail validation";

  // restore permissions for cleanup
  std::filesystem::permissions(readonlyFile,
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

// ============================================================================
// FEATURE: executeTransmute
// ============================================================================

TEST_F(PipelineTransmuteTest, ExecuteTransmuteHandlesEmptyRecipes)
{
  const std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      EmptyRecipes;

  auto result = alchemy::pipeline::executeTransmute(EmptyRecipes, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::empty recipes should succeed";
  ASSERT_EQ(result.value().recipesApplied, 0)
      << "alchemy::testing::unit::no recipes should be applied";
  ASSERT_EQ(result.value().filesProcessed, 0)
      << "alchemy::testing::unit::no files should be processed";
}

TEST_F(PipelineTransmuteTest, ExecuteTransmuteAppliesSingleFileRecipes)
{
  auto sourceFile = utils::createTestFile(tempDir, "source.h", "int x;");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[sourceFile.string()] = {
      utils::createRecipe(sourceFile, 0, 6, "double y;")};

  auto result = alchemy::pipeline::executeTransmute(recipes, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::single file transmutation should succeed";
  ASSERT_EQ(result.value().recipesApplied, 1)
      << "alchemy::testing::unit::one recipe should be applied";
  ASSERT_EQ(result.value().filesProcessed, 1)
      << "alchemy::testing::unit::one file should be processed";

  // verify file was actually mutated
  auto content = utils::readFile(sourceFile);
  ASSERT_EQ(content, "double y;")
      << "alchemy::testing::unit::file should be mutated with new content";
}

TEST_F(PipelineTransmuteTest, ExecuteTransmuteAppliesMultipleFilesRecipes)
{
  auto file1 = utils::createTestFile(tempDir, "file1.h", "int a;");
  auto file2 = utils::createTestFile(tempDir, "file2.h", "int b;");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[file1.string()] = {utils::createRecipe(file1, 0, 6, "float x;")};
  recipes[file2.string()] = {utils::createRecipe(file2, 0, 6, "float y;")};

  auto result = alchemy::pipeline::executeTransmute(recipes, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::multiple files transmutation should succeed";
  ASSERT_EQ(result.value().recipesApplied, 2)
      << "alchemy::testing::unit::two recipes should be applied";
  ASSERT_EQ(result.value().filesProcessed, 2)
      << "alchemy::testing::unit::two files should be processed";

  // verify both files were mutated
  ASSERT_EQ(utils::readFile(file1), "float x;")
      << "alchemy::testing::unit::file1 should be mutated";
  ASSERT_EQ(utils::readFile(file2), "float y;")
      << "alchemy::testing::unit::file2 should be mutated";
}

TEST_F(PipelineTransmuteTest, ExecuteTransmuteAppliesMultipleRecipesPerFile)
{
  auto sourceFile =
      utils::createTestFile(tempDir, "multi.h", "int a;\nint b;\n");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[sourceFile.string()] = {
      utils::createRecipe(sourceFile, 0, 6, "float x;"),
      utils::createRecipe(sourceFile, 7, 6, "float y;")};

  auto result = alchemy::pipeline::executeTransmute(recipes, false);

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::multiple recipes per "
                                 "file transmutation should succeed";
  ASSERT_EQ(result.value().recipesApplied, 2)
      << "alchemy::testing::unit::two recipes should be applied";
  ASSERT_EQ(result.value().filesProcessed, 1)
      << "alchemy::testing::unit::one file should be processed";

  // verify both recipes were applied
  auto content = utils::readFile(sourceFile);
  ASSERT_EQ(content, "float x;\nfloat y;\n")
      << "alchemy::testing::unit::both recipes should be applied to file";
}

TEST_F(PipelineTransmuteTest, ExecuteTransmuteDryRunDoesNotMutateFiles)
{
  auto sourceFile = utils::createTestFile(tempDir, "dryrun.h", "int x;");
  const std::string OriginalContent = utils::readFile(sourceFile);

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[sourceFile.string()] = {
      utils::createRecipe(sourceFile, 0, 6, "double y;")};

  auto result = alchemy::pipeline::executeTransmute(recipes, true);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::dry run should succeed";
  ASSERT_EQ(result.value().recipesApplied, 1)
      << "alchemy::testing::unit::dry run should count recipes";
  ASSERT_EQ(result.value().filesProcessed, 1)
      << "alchemy::testing::unit::dry run should count files";

  // verify file was NOT mutated
  auto content = utils::readFile(sourceFile);
  ASSERT_EQ(content, OriginalContent)
      << "alchemy::testing::unit::dry run should not mutate file";
}

// ============================================================================
// FEATURE: transmute (orchestrator)
// ============================================================================

TEST_F(PipelineTransmuteTest, TransmuteOrchestrationPassesForEmptyRecipes)
{
  const std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      EmptyRecipes;

  auto result = alchemy::pipeline::transmute(EmptyRecipes, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::transmute with empty recipes should succeed";
  ASSERT_EQ(result.value().recipesApplied, 0)
      << "alchemy::testing::unit::no recipes applied";
  ASSERT_EQ(result.value().filesProcessed, 0)
      << "alchemy::testing::unit::no files processed";
}

TEST_F(PipelineTransmuteTest, TransmuteOrchestrationFailsIfValidationFails)
{
  auto nonexistentFile = tempDir / "missing.h";

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[nonexistentFile.string()] = {
      utils::createRecipe(nonexistentFile, 0, 10, "// code")};

  auto result = alchemy::pipeline::transmute(recipes, false);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::transmute should fail if validation fails";
}

TEST_F(PipelineTransmuteTest, TransmuteOrchestrationSucceedsForValidRecipes)
{
  auto sourceFile = utils::createTestFile(tempDir, "valid.h", "int x;");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[sourceFile.string()] = {
      utils::createRecipe(sourceFile, 0, 6, "double y;")};

  auto result = alchemy::pipeline::transmute(recipes, false);

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::transmute should "
                                 "succeed for valid recipes";
  ASSERT_EQ(result.value().recipesApplied, 1)
      << "alchemy::testing::unit::one recipe applied";
  ASSERT_EQ(result.value().filesProcessed, 1)
      << "alchemy::testing::unit::one file processed";

  // verify orchestrator actually mutated file
  auto content = utils::readFile(sourceFile);
  ASSERT_EQ(content, "double y;")
      << "alchemy::testing::unit::orchestrator should apply mutation";
}

TEST_F(PipelineTransmuteTest, TransmuteOrchestrationDryRunDoesNotMutate)
{
  auto sourceFile = utils::createTestFile(tempDir, "dryrun.h", "int x;");
  const std::string OriginalContent = utils::readFile(sourceFile);

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[sourceFile.string()] = {
      utils::createRecipe(sourceFile, 0, 6, "double y;")};

  auto result = alchemy::pipeline::transmute(recipes, true);

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::transmute dry run "
                                 "should succeed";
  ASSERT_EQ(result.value().recipesApplied, 1)
      << "alchemy::testing::unit::dry run counts recipes";
  ASSERT_EQ(result.value().filesProcessed, 1)
      << "alchemy::testing::unit::dry run counts files";

  // verify orchestrator did NOT mutate file
  auto content = utils::readFile(sourceFile);
  ASSERT_EQ(content, OriginalContent)
      << "alchemy::testing::unit::orchestrator dry run should not mutate";
}

}  // namespace alchemy::testing
