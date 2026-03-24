// tests/unit/test_cli.cpp
// unit tests for CLI validation architecture
// uses black-box testing through Validator::validate() to maintain
// encapsulation

// std
#include <array>
#include <filesystem>
#include <thread>
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "cli/cli.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class CLIValidatorTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary directories for testing
    testDir = std::filesystem::temp_directory_path() / "alchemy_cli_test";
    buildDir = testDir / "build";
    outputDir = testDir / "output";

    std::filesystem::create_directories(buildDir);
    // note: don't create outputDir - we want to test creation
  }

  void
  TearDown() override
  {
    // cleanup test directories
    if (std::filesystem::exists(testDir))
    {
      std::filesystem::remove_all(testDir);
    }
  }

  std::filesystem::path testDir;
  std::filesystem::path buildDir;
  std::filesystem::path outputDir;
};

TEST_F(CLIValidatorTest, ValidateFailsWhenNoFeaturesEnabled)
{
  auto inputs = utils::createCliInputs(utils::MockCliConfig{
      .buildDir = buildDir,
      .outputDir = outputDir,
      .sourceFiles = {"test.c"},
      .excludePatterns = {},
      .jobs = 1
      // no features enabled
  });

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.invalid())
      << "validation should fail when no features enabled";
  ASSERT_TRUE(result.error().find("at least one feature") != std::string::npos)
      << "error should mention feature requirement";
}

TEST_F(CLIValidatorTest, ValidateSucceedsWithSalignOnly)
{
  auto inputs =
      utils::createCliInputs(utils::MockCliConfig{.buildDir = buildDir,
                                                  .outputDir = outputDir,
                                                  .sourceFiles = {"test.c"},
                                                  .excludePatterns = {},
                                                  .enableSalign = true,
                                                  .jobs = 1});

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.valid())
      << "validation should succeed with salign enabled";
  ASSERT_TRUE(result.value().enableSalign);
}

TEST_F(CLIValidatorTest, ValidateSalignDoesNotRequireOutputDir)
{
  auto inputs = utils::createCliInputs(utils::MockCliConfig{
      .buildDir = buildDir,
      .outputDir = "",  // empty, but salign doesn't need it
      .sourceFiles = {"test.c"},
      .excludePatterns = {},
      .enableSalign = true,
      .jobs = 1});

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.valid())
      << "salign should succeed without output dir (mutates in-place)";
  ASSERT_TRUE(result.value().outputDir.empty())
      << "output dir should remain empty for salign";
}

TEST_F(CLIValidatorTest, ValidateSalignWarnsWhenOutputDirProvided)
{
  // salign doesn't use outputDir, but user provided it - should warn
  auto inputs = utils::createCliInputs(utils::MockCliConfig{
      .buildDir = buildDir,
      .outputDir = outputDir,  // user provided, but salign ignores it
      .sourceFiles = {"test.c"},
      .excludePatterns = {},
      .enableSalign = true,
      .jobs = 1});

  // capture stderr to check for warning
  ::testing::internal::CaptureStderr();
  auto result = alchemy::cli::Validator::validate(std::move(inputs));
  const std::string StderrOutput = ::testing::internal::GetCapturedStderr();

  ASSERT_TRUE(result.valid())
      << "salign should succeed even with outputDir provided";
  ASSERT_EQ(result.value().outputDir, outputDir)
      << "outputDir preserved but unused by salign";
  ASSERT_TRUE(StderrOutput.find("warning") != std::string::npos)
      << "should warn user about ignored outputDir";
  ASSERT_TRUE(StderrOutput.find("salign") != std::string::npos)
      << "warning should mention salign";
  ASSERT_TRUE(StderrOutput.find("will ignore") != std::string::npos)
      << "warning should mention outputDir will be ignored";
}

TEST_F(CLIValidatorTest, ValidateMultipleFeaturesAllMissingSourcePatterns)
{
  // when all features missing same requirement, first feature reports error
  auto inputs = utils::createCliInputs(
      utils::MockCliConfig{.buildDir = buildDir,
                           .outputDir = outputDir,
                           .sourceFiles = {},  // all features need this!
                           .excludePatterns = {},
                           .enableSalign = true});

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.invalid()) << "requires source patterns";
  ASSERT_TRUE(result.error().find("source file patterns are required") !=
              std::string::npos);
}

TEST_F(CLIValidatorTest, ValidateAutoDetectsJobsWhenZero)
{
  auto inputs = utils::createCliInputs(utils::MockCliConfig{
      .buildDir = buildDir,
      .outputDir = outputDir,
      .sourceFiles = {"test.c"},
      .excludePatterns = {},
      .enableSalign = true,
      .jobs = 0  // auto-detect
  });

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.valid());
  const unsigned Expected = std::thread::hardware_concurrency();
  ASSERT_EQ(result.value().jobs, Expected)
      << "jobs=0 should auto-detect hardware_concurrency()";
}

TEST_F(CLIValidatorTest, ValidatePreservesExplicitJobCount)
{
  auto inputs =
      utils::createCliInputs(utils::MockCliConfig{.buildDir = buildDir,
                                                  .outputDir = outputDir,
                                                  .sourceFiles = {"test.c"},
                                                  .excludePatterns = {},
                                                  .enableSalign = true,
                                                  .jobs = 4});

  auto result = alchemy::cli::Validator::validate(std::move(inputs));

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().jobs, 4) << "explicit job count should be preserved";
}

// ============================================================================
// parseCli() tests (limited - avoids LLVM global state)
// ============================================================================

TEST_F(CLIValidatorTest, ParseCliHandlesInvalidFlag)
{
  std::array<const char*, 2> argv = {"alchemy",
                                     "--invalid-flag-that-does-not-exist"};
  const int Argc = 2;

  auto result = alchemy::cli::parseCli(Argc, argv.data());

  ASSERT_TRUE(result.invalid());
}

// ============================================================================
// CLI validation edge cases - warning coverage for all features
// ============================================================================

}  // namespace alchemy::testing
