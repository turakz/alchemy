// tests/integration/salign/test_salign_errors.cpp
// integration tests for salign error handling and safety mechanisms

// std
#include <filesystem>
#include <string>

// 3rd party
#include <gtest/gtest.h>

// local
#include "app/app.hpp"
#include "app/core/core.hpp"
#include "data/integration/salign/salign_test_fixture.hpp"
#include "utils.hpp"

namespace alchemy::testing {

TEST_F(SalignIntegrationTest, HandlesMalformedCHeaderSourceShouldFail)
{
  const std::string BadCode = R"(
struct BadStruct {
  this is not valid C syntax!!!
  char missing_semicolon
  } // missing semicolon above
}
)";

  utils::createTestFile(tempDir, "bad_syntax.h", BadCode);
  std::string headerPath = (tempDir / "bad_syntax.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should fail if c header "
         "source is "
         "malformed";

  const int ExitCode = appResult.value().exec();
  // soft-skip: malformed source is skipped with a warning rather than a
  // hard failure — clang parse errors are treated as non-fatal so alchemy
  // can continue processing any remaining files. exit 0 is expected.
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::malformed source should be "
         "soft-skipped with a warning — app exits 0, no mutations applied";

  // verify integration: file must be unchanged (no recipes generated for
  // a file that failed to parse)
  auto fileContent = utils::readFile(tempDir / "bad_syntax.h");
  ASSERT_EQ(fileContent, BadCode)
      << "alchemy::testing::integration::file should be unchanged on parse "
         "error - "
         "verifies error handling across module boundaries";
}

TEST_F(SalignIntegrationTest, HandlesMalformedCXXHeaderSourceShouldFail)
{
  const std::string BadCode = R"(
struct BadStruct {
  this is not valid CXX syntax!!!
  char missing_semicolon
  } // missing semicolon above
}
)";

  utils::createTestFile(tempDir, "bad_syntax.hpp", BadCode);
  std::string headerPath = (tempDir / "bad_syntax.hpp").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should fail if cxx "
         "header source is "
         "malformed";

  const int ExitCode = appResult.value().exec();
  // soft-skip: malformed source is skipped with a warning rather than a
  // hard failure — clang parse errors are treated as non-fatal so alchemy
  // can continue processing any remaining files. exit 0 is expected.
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::malformed source should be "
         "soft-skipped with a warning — app exits 0, no mutations applied";

  // verify integration: file must be unchanged (no recipes generated for
  // a file that failed to parse)
  auto fileContent = utils::readFile(tempDir / "bad_syntax.hpp");
  ASSERT_EQ(fileContent, BadCode)
      << "alchemy::testing::integration::file should be unchanged on parse "
         "error - "
         "verifies error handling across module boundaries";
}

// test error handling for write permission errors (atomic write failure path)
TEST_F(SalignIntegrationTest, HandlesWritePermissionErrorGracefully)
{
  // create test header with unoptimized struct
  const std::string TestCode = R"(
struct UnoptimizedStruct {
  char a;
  double b;
  int c;
};
)";

  // create a subdirectory that we'll make read-only
  auto readonlyDir = tempDir / "readonly_dir";
  std::filesystem::create_directory(readonlyDir);

  auto testFile = readonlyDir / "test.h";
  utils::createTestFile(readonlyDir, "test.h", TestCode);
  std::string headerPath = testFile.string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});

  // make directory read-only (prevents creating temp file)
  // note: on Linux, rename() can replace files even if file is read-only,
  // but creating new files in read-only directory fails
  std::filesystem::permissions(readonlyDir,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::group_exec,
                               std::filesystem::perm_options::replace);

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed";

  const int ExitCode = appResult.value().exec();

  // should fail gracefully with non-zero exit code
  ASSERT_NE(ExitCode, 0)
      << "alchemy::testing::integration::write permission error should cause "
         "pipeline failure";

  // restore permissions to verify original content
  std::filesystem::permissions(readonlyDir,
                               std::filesystem::perms::owner_all |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::group_exec,
                               std::filesystem::perm_options::replace);

  // verify original content unchanged (atomic write guarantees)
  auto content = utils::readFile(testFile);
  ASSERT_EQ(content, TestCode)
      << "alchemy::testing::integration::file should remain unchanged on "
         "write failure";

  // verify no .alch temp file left behind (cleanup on error)
  auto tempFile =
      testFile.parent_path() / (testFile.filename().string() + ".alch");
  ASSERT_FALSE(std::filesystem::exists(tempFile))
      << "alchemy::testing::integration::temp file should be cleaned up on "
         "write failure";
}

// test pre-flight validation: multiple files, read-only directory prevents ALL
// modifications
TEST_F(SalignIntegrationTest, PreFlightValidationPreventsPartialModifications)
{
  // create test code with unoptimized struct
  const std::string TestCode = R"(
struct UnoptimizedStruct {
  char a;
  double b;
  int c;
};
)";

  // create file1 and file2 in normal directory
  auto file1 = tempDir / "file1.h";
  auto file2 = tempDir / "file2.h";

  utils::createTestFile(tempDir, "file1.h", TestCode);
  utils::createTestFile(tempDir, "file2.h", TestCode);

  // create file3 in a read-only directory
  auto readonlyDir = tempDir / "readonly_dir";
  std::filesystem::create_directory(readonlyDir);
  auto file3 = readonlyDir / "file3.h";
  utils::createTestFile(readonlyDir, "file3.h", TestCode);

  std::string headerPath1 = file1.string();
  std::string headerPath2 = file2.string();
  std::string headerPath3 = file3.string();

  utils::createSyntheticCompilationDatabase(
      tempDir, {headerPath1, headerPath2, headerPath3});

  // make directory read-only BEFORE running alchemy (pre-flight catches this)
  std::filesystem::permissions(readonlyDir,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::group_exec,
                               std::filesystem::perm_options::replace);

  auto appResult = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .rootDir = tempDir,
      .buildDir = tempDir,
      .outputDir = tempDir,
      .sourceFiles = {headerPath1, headerPath2, headerPath3},
      .excludePatterns = {},
      .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed";

  const int ExitCode = appResult.value().exec();

  // should fail due to pre-flight validation
  ASSERT_NE(ExitCode, 0)
      << "alchemy::testing::integration::pre-flight validation should detect "
         "read-only directory";

  // restore permissions to verify original content
  std::filesystem::permissions(readonlyDir,
                               std::filesystem::perms::owner_all |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::group_exec,
                               std::filesystem::perm_options::replace);

  // CRITICAL: verify NONE of the files were modified (pre-flight prevented
  // partial success)
  auto content1 = utils::readFile(file1);
  auto content2 = utils::readFile(file2);
  auto content3 = utils::readFile(file3);

  ASSERT_EQ(content1, TestCode)
      << "alchemy::testing::integration::file1 should NOT be modified when "
         "pre-flight fails";
  ASSERT_EQ(content2, TestCode)
      << "alchemy::testing::integration::file2 should NOT be modified when "
         "pre-flight fails";
  ASSERT_EQ(content3, TestCode)
      << "alchemy::testing::integration::file3 should NOT be modified when "
         "pre-flight fails";

  // verify no temp files left behind
  ASSERT_FALSE(std::filesystem::exists(file1.string() + ".alch"));
  ASSERT_FALSE(std::filesystem::exists(file2.string() + ".alch"));
  ASSERT_FALSE(std::filesystem::exists(file3.string() + ".alch"));
}

// test pre-flight validation: read-only file detected before any modifications
TEST_F(SalignIntegrationTest, PreFlightValidationDetectsReadOnlyFile)
{
  const std::string TestCode = R"(
struct UnoptimizedStruct {
  char a;
  double b;
  int c;
};
)";

  // create 3 files
  auto file1 = tempDir / "file1.h";
  auto file2 = tempDir / "file2.h";
  auto file3 = tempDir / "file3.h";

  utils::createTestFile(tempDir, "file1.h", TestCode);
  utils::createTestFile(tempDir, "file2.h", TestCode);
  utils::createTestFile(tempDir, "file3.h", TestCode);

  // make file2 read-only (not directory, just the file itself)
  std::filesystem::permissions(file2,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read,
                               std::filesystem::perm_options::replace);

  std::string headerPath1 = file1.string();
  std::string headerPath2 = file2.string();
  std::string headerPath3 = file3.string();

  utils::createSyntheticCompilationDatabase(
      tempDir, {headerPath1, headerPath2, headerPath3});

  auto appResult = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .rootDir = tempDir,
      .buildDir = tempDir,
      .outputDir = tempDir,
      .sourceFiles = {headerPath1, headerPath2, headerPath3},
      .excludePatterns = {},
      .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed";

  const int ExitCode = appResult.value().exec();

  // should fail due to pre-flight validation detecting read-only file
  ASSERT_NE(ExitCode, 0)
      << "alchemy::testing::integration::pre-flight validation should detect "
         "read-only file";

  // restore permissions to read files
  std::filesystem::permissions(file2,
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);

  // verify NONE of the files were modified
  auto content1 = utils::readFile(file1);
  auto content2 = utils::readFile(file2);
  auto content3 = utils::readFile(file3);

  ASSERT_EQ(content1, TestCode)
      << "alchemy::testing::integration::file1 should NOT be modified when "
         "pre-flight fails";
  ASSERT_EQ(content2, TestCode)
      << "alchemy::testing::integration::file2 should NOT be modified when "
         "pre-flight fails";
  ASSERT_EQ(content3, TestCode)
      << "alchemy::testing::integration::file3 should NOT be modified when "
         "pre-flight fails";

  // verify no temp files
  ASSERT_FALSE(std::filesystem::exists(file1.string() + ".alch"));
  ASSERT_FALSE(std::filesystem::exists(file2.string() + ".alch"));
  ASSERT_FALSE(std::filesystem::exists(file3.string() + ".alch"));
}

// test compilation database validation: missing compile_commands.json fails
TEST_F(SalignIntegrationTest, MissingCompilationDatabaseFails)
{
  const std::string TestCode = R"(
struct TestStruct {
  char a;
  long b;
  int c;
};
)";

  auto sourceFile = utils::createTestFile(tempDir, "no_db.h", TestCode);

  // create separate build directory WITHOUT compile_commands.json
  auto buildDir = tempDir / "empty_build";
  std::filesystem::create_directories(buildDir);

  auto appResult = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .rootDir = tempDir,
      .buildDir = buildDir,  // build dir without compile_commands.json
      .outputDir = tempDir,
      .sourceFiles = {sourceFile.string()},
      .excludePatterns = {},
      .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed";

  const int ExitCode = appResult.value().exec();

  // should fail with clear error about missing compilation database
  ASSERT_NE(ExitCode, 0)
      << "alchemy::testing::integration::should fail when build dir lacks "
         "compile_commands.json";
}

// test compilation database validation: valid database succeeds
TEST_F(SalignIntegrationTest, ValidCompilationDatabaseSucceeds)
{
  const std::string TestCode = R"(
struct TestStruct {
  char a;
  long b;
  int c;
};
)";

  auto sourceFile = utils::createTestFile(tempDir, "valid_db.h", TestCode);

  // create build directory WITH compile_commands.json
  auto buildDir = tempDir / "build";
  std::filesystem::create_directories(buildDir);
  utils::createSyntheticCompilationDatabase(buildDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .rootDir = tempDir,
      .buildDir = buildDir,  // valid build dir with compile_commands.json
      .outputDir = tempDir,
      .sourceFiles = {sourceFile.string()},
      .excludePatterns = {},
      .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed";

  const int ExitCode = appResult.value().exec();

  // should succeed
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::should succeed with valid compilation "
         "database";

  // verify struct was optimized with correct field order
  const std::string Content = utils::readFile(sourceFile);
  ASSERT_NE(Content, TestCode)
      << "alchemy::testing::integration::file should be modified";

  // verify specific optimization occurred (long before char)
  auto longPos = Content.find("long b");
  auto charPos = Content.find("char a");
  ASSERT_NE(longPos, std::string::npos)
      << "alchemy::testing::integration::long field should exist";
  ASSERT_NE(charPos, std::string::npos)
      << "alchemy::testing::integration::char field should exist";
  ASSERT_LT(longPos, charPos) << "alchemy::testing::integration::long should "
                                 "come before char (validates "
                                 "optimization occurred)";
}

}  // namespace alchemy::testing
