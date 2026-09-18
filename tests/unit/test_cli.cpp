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
    testDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_cli_test", false);
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
      .rootDir = testDir,
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
      utils::createCliInputs(utils::MockCliConfig{.rootDir = testDir,
                                                  .buildDir = buildDir,
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
      .rootDir = testDir,
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
      .rootDir = testDir,
      .buildDir = buildDir,
      .outputDir = outputDir,  // user provided, but salign ignores it
      .sourceFiles = {"test.c"},
      .excludePatterns = {},
      .enableSalign = true,
      .jobs = 1});

  // capture stdout to check for warning (logger::warn routes to stdout)
  ::testing::internal::CaptureStdout();
  auto result = alchemy::cli::Validator::validate(std::move(inputs));
  const std::string StdoutOutput = ::testing::internal::GetCapturedStdout();

  ASSERT_TRUE(result.valid())
      << "salign should succeed even with outputDir provided";
  ASSERT_EQ(result.value().outputDir, outputDir)
      << "outputDir preserved but unused by salign";
  ASSERT_TRUE(StdoutOutput.find("warning") != std::string::npos)
      << "should warn user about ignored outputDir";
  ASSERT_TRUE(StdoutOutput.find("salign") != std::string::npos)
      << "warning should mention salign";
  ASSERT_TRUE(StdoutOutput.find("will ignore") != std::string::npos)
      << "warning should mention outputDir will be ignored";
}

TEST_F(CLIValidatorTest, ValidateMultipleFeaturesAllMissingSourcePatterns)
{
  // when all features missing same requirement, first feature reports error
  auto inputs = utils::createCliInputs(
      utils::MockCliConfig{.rootDir = testDir,
                           .buildDir = buildDir,
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
      .rootDir = testDir,
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
      utils::createCliInputs(utils::MockCliConfig{.rootDir = testDir,
                                                  .buildDir = buildDir,
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

  // LLVM's ParseCommandLineOptions calls exit(1) on unrecognized flags,
  // printing the error to stderr before terminating
  ASSERT_DEATH(alchemy::cli::parseCli(Argc, argv.data()),
               "Unknown command line argument");
}

// ============================================================================
// mergeInputs() tests — CLI args take precedence over config file values
// ============================================================================

TEST_F(CLIValidatorTest, MergeInputsBuildDirTakenFromConfigWhenCliEmpty)
{
  alchemy::cli::CliInputs cli;
  cli.paths.buildDir = "";  // CLI did not provide build dir

  alchemy::cli::CliInputs config;
  config.paths.buildDir = buildDir.string();  // config has a build dir

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, buildDir.string())
      << "build dir should be taken from config when CLI does not provide one";
}

TEST_F(CLIValidatorTest, MergeInputsBuildDirCLITakesPrecedenceOverConfig)
{
  alchemy::cli::CliInputs cli;
  cli.paths.buildDir = buildDir.string();  // CLI provided build dir

  alchemy::cli::CliInputs config;
  config.paths.buildDir = (testDir / "other_build").string();  // different

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, buildDir.string())
      << "CLI build dir must take precedence over config build dir";
}

TEST_F(CLIValidatorTest, MergeInputsOutputDirTakenFromConfigWhenCliEmpty)
{
  alchemy::cli::CliInputs cli;
  cli.paths.outputDir = "";

  alchemy::cli::CliInputs config;
  config.paths.outputDir = outputDir.string();

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.outputDir, outputDir.string())
      << "output dir should be taken from config when CLI does not provide one";
}

TEST_F(CLIValidatorTest, MergeInputsSourcePatternsTakenFromConfigWhenCliEmpty)
{
  alchemy::cli::CliInputs cli;
  cli.paths.sourcePatterns = {};

  alchemy::cli::CliInputs config;
  config.paths.sourcePatterns = {"Source/**/*.h", "Include/**/*.h"};

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.sourcePatterns.size(), 2u)
      << "source patterns should be taken from config when CLI provides none";
  ASSERT_EQ(merged.paths.sourcePatterns[0], "Source/**/*.h");
  ASSERT_EQ(merged.paths.sourcePatterns[1], "Include/**/*.h");
}

TEST_F(CLIValidatorTest, MergeInputsSourcePatternsCLITakesPrecedenceOverConfig)
{
  alchemy::cli::CliInputs cli;
  cli.paths.sourcePatterns = {"src/*.h"};  // CLI provided

  alchemy::cli::CliInputs config;
  config.paths.sourcePatterns = {"Source/**/*.h"};  // config has different

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.sourcePatterns.size(), 1u);
  ASSERT_EQ(merged.paths.sourcePatterns[0], "src/*.h")
      << "CLI source patterns must take precedence over config";
}

TEST_F(CLIValidatorTest, MergeInputsExcludePatternsTakenFromConfigWhenCliEmpty)
{
  alchemy::cli::CliInputs cli;
  cli.paths.excludePatterns = {};

  alchemy::cli::CliInputs config;
  config.paths.excludePatterns = {"**/test/**", "**/mock/**"};

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.excludePatterns.size(), 2u)
      << "exclude patterns should be taken from config when CLI provides none";
  ASSERT_EQ(merged.paths.excludePatterns[0], "**/test/**");
  ASSERT_EQ(merged.paths.excludePatterns[1], "**/mock/**");
}

TEST_F(CLIValidatorTest, MergeInputsSalignTakenFromConfigWhenCliDisabled)
{
  alchemy::cli::CliInputs cli;
  cli.features.enableSalign = false;  // CLI did not set --salign

  alchemy::cli::CliInputs config;
  config.features.enableSalign = true;  // config has salign = true

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_TRUE(merged.features.enableSalign)
      << "salign should be enabled from config when CLI did not set it";
}

TEST_F(CLIValidatorTest, MergeInputsJobsTakenFromConfigWhenCliIsDefault)
{
  alchemy::cli::CliInputs cli;
  cli.jobs = 1;  // CLI default

  alchemy::cli::CliInputs config;
  config.jobs = 8;  // config specifies 8 jobs

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.jobs, 8u)
      << "jobs should be taken from config when CLI has the default value (1)";
}

TEST_F(CLIValidatorTest, MergeInputsJobsCLITakesPrecedenceWhenBothNonDefault)
{
  alchemy::cli::CliInputs cli;
  cli.jobs = 4;  // CLI explicitly set jobs

  alchemy::cli::CliInputs config;
  config.jobs = 8;  // config also set jobs

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  // CLI jobs != 1 → CLI takes precedence (config not applied)
  ASSERT_EQ(merged.jobs, 4u)
      << "CLI jobs must take precedence when it is not the default (1)";
}

TEST_F(CLIValidatorTest, MergeInputsDryRunTakenFromConfigWhenCliDisabled)
{
  alchemy::cli::CliInputs cli;
  cli.enableDryRun = false;

  alchemy::cli::CliInputs config;
  config.enableDryRun = true;

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_TRUE(merged.enableDryRun)
      << "dry-run should be enabled from config when CLI did not set it";
}

TEST_F(CLIValidatorTest, MergeInputsAllFieldsFromConfigWhenCliIsEmpty)
{
  // verify all fields are merged in a single call when CLI provides nothing
  alchemy::cli::CliInputs cli;

  alchemy::cli::CliInputs config;
  config.paths.buildDir = buildDir.string();
  config.paths.outputDir = outputDir.string();
  config.paths.sourcePatterns = {"Source/**/*.h"};
  config.paths.excludePatterns = {"**/test/**"};
  config.features.enableSalign = true;
  config.jobs = 4;
  config.enableDryRun = true;

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, buildDir.string());
  ASSERT_EQ(merged.paths.outputDir, outputDir.string());
  ASSERT_EQ(merged.paths.sourcePatterns.size(), 1u);
  ASSERT_EQ(merged.paths.excludePatterns.size(), 1u);
  ASSERT_TRUE(merged.features.enableSalign);
  ASSERT_EQ(merged.jobs, 4u);
  ASSERT_TRUE(merged.enableDryRun);
}

// ============================================================================
// CLI validation edge cases - warning coverage for all features
// ============================================================================

}  // namespace alchemy::testing
