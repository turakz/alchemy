// tests/unit/test_preflight_validator.cpp

// std
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "operation/operation_base.hpp"
#include "pipeline/preflight_validator.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class PreflightValidatorTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temp directory for tests
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_preflight_test");
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
// FEATURE: checkIfFileExists
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIfFileExistsPassesForExistingFile)
{
  auto testFile = utils::createTestFile(tempDir, "exists.txt", "content");

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIfFileExists(
          testFile);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::existing file should pass check";
}

TEST_F(PreflightValidatorTest, CheckIfFileExistsFailsForNonexistentFile)
{
  auto nonexistentFile = tempDir / "does_not_exist.txt";

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIfFileExists(
          nonexistentFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::nonexistent file should fail check";
  ASSERT_NE(result.error().find("does not exist"), std::string::npos)
      << "alchemy::testing::unit::error message should mention file doesn't "
         "exist";
}

// ============================================================================
// FEATURE: checkIsRegularFile
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIsRegularFilePassesForRegularFile)
{
  auto testFile = utils::createTestFile(tempDir, "regular.txt", "content");

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsRegularFile(
          testFile);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::regular file should pass check";
}

TEST_F(PreflightValidatorTest, CheckIsRegularFileFailsForDirectory)
{
  auto directory = tempDir / "subdir";
  std::filesystem::create_directory(directory);

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsRegularFile(
          directory);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::directory should fail regular file check";
  ASSERT_NE(result.error().find("not a regular file"), std::string::npos)
      << "alchemy::testing::unit::error message should mention not a regular "
         "file";
}

// ============================================================================
// FEATURE: checkIsFileWritable
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIsFileWritablePassesForWritableFile)
{
  auto testFile = utils::createTestFile(tempDir, "writable.txt", "content");

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsFileWritable(
          testFile);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::writable file should pass check";
}

TEST_F(PreflightValidatorTest, CheckIsFileWritableFailsForReadOnlyFile)
{
  auto testFile = utils::createTestFile(tempDir, "readonly.txt", "content");

  // make file read-only
  std::filesystem::permissions(testFile,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace);

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsFileWritable(
          testFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::read-only file should fail writable check";
  ASSERT_NE(result.error().find("not writable"), std::string::npos)
      << "alchemy::testing::unit::error message should mention not writable";

  // restore permissions for cleanup
  std::filesystem::permissions(testFile,
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

// ============================================================================
// FEATURE: checkIsParentDirWritable
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIsParentDirWritablePassesForWritableParent)
{
  auto testFile = utils::createTestFile(tempDir, "file.txt", "content");

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsParentDirWritable(
          testFile);

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::file in writable "
                                 "directory should pass check";
}

TEST_F(PreflightValidatorTest, CheckIsParentDirWritableFailsForReadOnlyParent)
{
  auto subdir = tempDir / "readonly_dir";
  std::filesystem::create_directory(subdir);

  auto testFile = utils::createTestFile(subdir, "file.txt", "content");

  // make parent directory read-only
  std::filesystem::permissions(subdir,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_read |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::replace);

  auto result =
      alchemy::pipeline::preflight_validator::detail::checkIsParentDirWritable(
          testFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::file in read-only directory should fail "
         "check";
  ASSERT_NE(result.error().find("not writable"), std::string::npos)
      << "alchemy::testing::unit::error message should mention directory not "
         "writable";

  // restore permissions for cleanup
  std::filesystem::permissions(subdir,
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

// ============================================================================
// FEATURE: checkIfCanApplyRecipe (orchestrator)
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipePassesForValidFile)
{
  auto testFile = utils::createTestFile(tempDir, "valid.txt", "content");

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(testFile);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::valid file should pass all checks";
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipeFailsIfFileDoesNotExist)
{
  auto nonexistentFile = tempDir / "missing.txt";

  auto result = alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(
      nonexistentFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::should fail if file doesn't exist";
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipeFailsIfNotRegularFile)
{
  auto directory = tempDir / "notfile";
  std::filesystem::create_directory(directory);

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(directory);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::should fail if not regular file";
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipeFailsIfFileNotWritable)
{
  auto testFile = utils::createTestFile(tempDir, "readonly.txt", "content");

  std::filesystem::permissions(testFile,
                               std::filesystem::perms::owner_read,
                               std::filesystem::perm_options::replace);

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(testFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::should fail if file not writable";

  // restore for cleanup
  std::filesystem::permissions(testFile,
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipeFailsIfParentDirNotWritable)
{
  auto subdir = tempDir / "readonly_parent";
  std::filesystem::create_directory(subdir);

  auto testFile = utils::createTestFile(subdir, "file.txt", "content");

  // make parent read-only
  std::filesystem::permissions(subdir,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipe(testFile);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::should fail if parent directory not "
         "writable";

  // restore for cleanup
  std::filesystem::permissions(subdir,
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

// ============================================================================
// FEATURE: checkIfCanApplyRecipes (batch validator)
// ============================================================================

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipesPassesForAllValidFiles)
{
  auto file1 = utils::createTestFile(tempDir, "file1.txt", "content1");
  auto file2 = utils::createTestFile(tempDir, "file2.txt", "content2");
  auto file3 = utils::createTestFile(tempDir, "file3.txt", "content3");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[file1.string()] = {};
  recipes[file2.string()] = {};
  recipes[file3.string()] = {};

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipes(recipes);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::should pass if all files are valid";
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipesFailsIfAnyFileInvalid)
{
  auto validFile1 = utils::createTestFile(tempDir, "valid1.txt", "content");
  auto invalidFile = tempDir / "nonexistent.txt";  // doesn't exist
  auto validFile2 = utils::createTestFile(tempDir, "valid2.txt", "content");

  std::unordered_map<std::string, std::vector<alchemy::operation::Recipe>>
      recipes;
  recipes[validFile1.string()] = {};
  recipes[invalidFile.string()] = {};  // this one will fail
  recipes[validFile2.string()] = {};

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipes(recipes);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::should fail if any file is invalid";
}

TEST_F(PreflightValidatorTest, CheckIfCanApplyRecipesPassesForEmptyMap)
{
  const std::unordered_map<std::string,
                           std::vector<alchemy::operation::Recipe>>
      Recipes;  // empty

  auto result =
      alchemy::pipeline::preflight_validator::checkIfCanApplyRecipes(Recipes);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::empty recipes map should pass";
}

}  // namespace alchemy::testing
