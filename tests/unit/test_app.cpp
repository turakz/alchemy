// tests/unit/test_app.cpp

// std
#include <algorithm>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

// 3rd party
#include <fmt/core.h>
#include <gtest/gtest.h>

// local
#include "app/app.hpp"
#include "operation/refactoring/salign_operation.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class AppTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temp directory for tests
    tempDir =
        alchemy::testing::utils::createTempTestDirectory("alchemy_app_test");
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
// FEATURE: App::create - good instantiations
// ============================================================================

TEST_F(AppTest, CreateSucceedsWithValidSingleSourceFile)
{
  // create a test source file
  auto testFile = utils::createTestFile(tempDir, "test.c", "// test");

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {testFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .jobs = 1});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid()) << "App creation should succeed with valid file";
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 1);
  ASSERT_EQ(result.value().context().inventory.sourceFiles[0], testFile);
}

TEST_F(AppTest, CreateSucceedsWithMultipleSourceFiles)
{
  auto file1 = utils::createTestFile(tempDir, "file1.c", "// file1");
  auto file2 = utils::createTestFile(tempDir, "file2.c", "// file2");

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {file1.string(), file2.string()},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .jobs = 1});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 2);
  // verify actual file paths are present (order may vary)
  const auto& files = result.value().context().inventory.sourceFiles;
  ASSERT_TRUE(std::find(std::begin(files), std::end(files), file1) !=
              std::end(files))
      << "should contain file1";
  ASSERT_TRUE(std::find(std::begin(files), std::end(files), file2) !=
              std::end(files))
      << "should contain file2";
}

TEST_F(AppTest, CreateSucceedsWithGlobPattern)
{
  // create multiple .c files
  utils::createTestFile(tempDir, "source1.c", "// source1");
  utils::createTestFile(tempDir, "source2.c", "// source2");
  utils::createTestFile(tempDir, "header.h", "// header");

  auto globPattern = (tempDir / "*.c").string();

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {globPattern},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .jobs = 1});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid());
  // should discover 2 .c files
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 2);
  // verify only .c files matched (not .h)
  for (const auto& file : result.value().context().inventory.sourceFiles)
  {
    ASSERT_EQ(file.extension(), ".c")
        << "glob *.c should only match .c files: " << file;
  }
}

// ============================================================================
// FEATURE: App::create - edge cases (discovery always succeeds)
// ============================================================================

TEST_F(AppTest, CreateSucceedsWithNoMatchingFiles)
{
  // glob pattern that matches nothing - discovery succeeds but returns empty
  auto invalidGlob = (tempDir / "*.nonexistent").string();

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {invalidGlob},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .jobs = 1});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid()) << "Discovery succeeds even with no matches";
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 0);
}

TEST_F(AppTest, CreateHandlesExcludePatterns)
{
  auto include1 = utils::createTestFile(tempDir, "include.c", "// include");
  auto exclude1 = utils::createTestFile(tempDir, "exclude.c", "// exclude");

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {(tempDir / "*.c").string()},
                           .excludePatterns = {exclude1.string()},
                           .enableSalign = true,
                           .jobs = 1});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 1);
  ASSERT_EQ(result.value().context().inventory.sourceFiles[0], include1);
  ASSERT_EQ(result.value().context().inventory.excludedFiles.size(), 1);
  ASSERT_EQ(result.value().context().inventory.excludedFiles[0], exclude1);
}

TEST_F(AppTest, CreateHandlesMultipleJobs)
{
  // create several files to test parallel discovery
  for (int i = 0; i < 10; ++i)
  {
    utils::createTestFile(tempDir, fmt::format("file{}.c", i), "// source");
  }

  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = tempDir / "output",
                           .sourceFiles = {(tempDir / "*.c").string()},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .jobs = 4});

  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().context().inventory.sourceFiles.size(), 10);
}

// ============================================================================
// FEATURE: createRecipeOperations
// ============================================================================

TEST_F(AppTest, CreateRecipeOperationsCreatesSalignOperation)
{
  alchemy::config::AppConfig config;
  config.cliArgs.enableSalign = true;

  auto testFile = utils::createTestFile(tempDir, "test.c", "// test");
  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = {},
                           .sourceFiles = {testFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  auto app = alchemy::App::create(std::move(options));
  ASSERT_TRUE(app.valid());

  auto operations = app.value().createRecipeOperations(config);

  ASSERT_EQ(operations.size(), 1);
  // verify it's a StructAlignmentOperation by visiting the variant
  std::visit(
      [](auto&& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T,
                                     alchemy::operation::refactoring::
                                         StructAlignmentOperation>)
        {
          SUCCEED();
        }
        else
        {
          FAIL() << "Expected StructAlignmentOperation";
        }
      },
      operations[0]);
}

TEST_F(AppTest, CreateRecipeOperationsCreatesSingleOperation)
{
  alchemy::config::AppConfig config;
  config.cliArgs.enableSalign = true;

  auto testFile = utils::createTestFile(tempDir, "test.c", "// test");
  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
                           .outputDir = {},
                           .sourceFiles = {testFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  auto app = alchemy::App::create(std::move(options));
  ASSERT_TRUE(app.valid());

  auto operations = app.value().createRecipeOperations(config);

  // salign is the only operation
  ASSERT_EQ(operations.size(), 1);
}

TEST_F(AppTest, CreateRecipeOperationsReturnsEmptyWhenConfigHasNoFeatures)
{
  const alchemy::config::AppConfig Config;
  // no features enabled

  auto testFile = utils::createTestFile(tempDir, "test.c", "// test");
  auto buildDir = utils::createTestFile(
      tempDir / "build", "compile_commands.json", "// test");
  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = buildDir,
                           .outputDir = {},
                           .sourceFiles = {testFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  auto app = alchemy::App::create(std::move(options));
  ASSERT_TRUE(app.valid());

  auto operations = app.value().createRecipeOperations(Config);

  ASSERT_TRUE(operations.empty());
}

TEST_F(AppTest, CreateFailsWithNonexistentBuildDir)
{
  auto testFile = utils::createTestFile(tempDir, "test.c", "// test");
  auto options = utils::createMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = "/nonexistent/path",
                           .outputDir = {},
                           .sourceFiles = {testFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});
  auto result = alchemy::App::create(std::move(options));

  ASSERT_TRUE(result.invalid());
}

}  // namespace alchemy::testing
