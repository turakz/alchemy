// tests/unit/test_config_parser.cpp
// unit tests for config file loading and CLI/config merge logic

// std
#include <cstddef>

#include <filesystem>
#include <string>
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "cli/cli.hpp"
#include "cli/config_parser.hpp"
#include "utils.hpp"

namespace alchemy::testing {

// ============================================================================
// loadConfig tests
// ============================================================================

class ConfigParserTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    testDir =
        alchemy::testing::utils::createTempTestDirectory("alchemy_config_test");
  }

  void
  TearDown() override
  {
    if (std::filesystem::exists(testDir))
    {
      std::filesystem::remove_all(testDir);
    }
  }

  std::filesystem::path
  writeConfig(const std::string& content)
  {
    auto configPath = testDir / "alchemy.toml";
    utils::createTestFile(testDir, "alchemy.toml", content);
    return configPath;
  }

  std::filesystem::path testDir;
};

TEST_F(ConfigParserTest, LoadConfigParsesAllOptions)
{
  // create directories so canonical() succeeds
  auto buildDir = testDir / "build";
  auto outputDir = testDir / "output";
  std::filesystem::create_directories(buildDir);
  std::filesystem::create_directories(outputDir);

  auto configPath = writeConfig("[options]\n"
                                "build-dir = \"" +
                                buildDir.string() +
                                "\"\n"
                                "output-dir = \"" +
                                outputDir.string() +
                                "\"\n"
                                "sources = [\"src/*.c\", \"lib/*.c\"]\n"
                                "exclude = [\"test/*\", \"vendor/*\"]\n"
                                "salign = true\n"
                                "dry-run = true\n"
                                "jobs = 4\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid()) << "valid TOML should parse successfully";

  const auto& inputs = result.value();
  ASSERT_EQ(inputs.paths.buildDir, buildDir.string());
  ASSERT_EQ(inputs.paths.outputDir, outputDir.string());
  ASSERT_EQ(inputs.paths.sourcePatterns.size(), 2);
  ASSERT_EQ(inputs.paths.sourcePatterns[0], "src/*.c");
  ASSERT_EQ(inputs.paths.sourcePatterns[1], "lib/*.c");
  ASSERT_EQ(inputs.paths.excludePatterns.size(), 2);
  ASSERT_EQ(inputs.paths.excludePatterns[0], "test/*");
  ASSERT_EQ(inputs.paths.excludePatterns[1], "vendor/*");
  ASSERT_TRUE(inputs.features.enableSalign);
  ASSERT_TRUE(inputs.enableDryRun);
  ASSERT_EQ(inputs.jobs, 4);
}

TEST_F(ConfigParserTest, LoadConfigDefaultsForMissingKeys)
{
  auto configPath = writeConfig("[options]\n"
                                "salign = true\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid()) << "partial config should parse successfully";

  const auto& inputs = result.value();
  ASSERT_TRUE(inputs.paths.buildDir.empty())
      << "missing build-dir should default to empty";
  ASSERT_TRUE(inputs.paths.outputDir.empty())
      << "missing output-dir should default to empty";
  ASSERT_TRUE(inputs.paths.sourcePatterns.empty())
      << "missing sources should default to empty";
  ASSERT_TRUE(inputs.paths.excludePatterns.empty())
      << "missing excludes should default to empty";
  ASSERT_TRUE(inputs.features.enableSalign);
  ASSERT_FALSE(inputs.enableDryRun)
      << "missing dry-run should default to false";
  ASSERT_EQ(inputs.jobs, 1) << "missing jobs should default to 1";
}

TEST_F(ConfigParserTest, LoadConfigDefaultsForEmptyOptionsTable)
{
  auto configPath = writeConfig("[options]\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid()) << "empty options table should parse";

  const auto& inputs = result.value();
  ASSERT_TRUE(inputs.paths.buildDir.empty());
  ASSERT_TRUE(inputs.paths.outputDir.empty());
  ASSERT_TRUE(inputs.paths.sourcePatterns.empty());
  ASSERT_TRUE(inputs.paths.excludePatterns.empty());
  ASSERT_FALSE(inputs.features.enableSalign);
  ASSERT_FALSE(inputs.enableDryRun);
  ASSERT_EQ(inputs.jobs, 1);
}

TEST_F(ConfigParserTest, LoadConfigDefaultsForEmptyFile)
{
  auto configPath = writeConfig("");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid())
      << "empty config file should parse (all defaults)";

  const auto& inputs = result.value();
  ASSERT_FALSE(inputs.features.enableSalign);
  ASSERT_EQ(inputs.jobs, 1);
}

TEST_F(ConfigParserTest, LoadConfigFailsOnInvalidToml)
{
  auto configPath = writeConfig("[options\n"
                                "this is not valid toml\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.invalid()) << "invalid TOML should fail";
  ASSERT_TRUE(result.error().find("config parse failed") != std::string::npos)
      << "error should mention parse failure";
}

TEST_F(ConfigParserTest, LoadConfigFailsOnNonExistentFile)
{
  auto configPath = testDir / "does_not_exist.toml";

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.invalid()) << "missing file should fail";
}

TEST_F(ConfigParserTest, LoadConfigParsesSourcesArray)
{
  auto configPath = writeConfig("[options]\n"
                                "sources = [\"a.c\", \"b.c\", \"c.c\"]\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().paths.sourcePatterns.size(), 3);
  ASSERT_EQ(result.value().paths.sourcePatterns[0], "a.c");
  ASSERT_EQ(result.value().paths.sourcePatterns[1], "b.c");
  ASSERT_EQ(result.value().paths.sourcePatterns[2], "c.c");
}

TEST_F(ConfigParserTest, LoadConfigParsesExcludesArray)
{
  auto configPath = writeConfig("[options]\n"
                                "exclude = [\"test/*\", \"build/*\"]\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().paths.excludePatterns.size(), 2);
  ASSERT_EQ(result.value().paths.excludePatterns[0], "test/*");
  ASSERT_EQ(result.value().paths.excludePatterns[1], "build/*");
}

TEST_F(ConfigParserTest, LoadConfigJobsZeroMeansAutoDetect)
{
  auto configPath = writeConfig("[options]\n"
                                "jobs = 0\n");

  auto result = alchemy::config::parser::loadConfig(configPath);

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().jobs, 0)
      << "jobs=0 should be preserved (auto-detect handled by Validator)";
}

// ============================================================================
// mergeInputs tests
// ============================================================================

class MergeInputsTest : public ::testing::Test {
protected:
  alchemy::cli::CliInputs
  makeCliInputs(const std::string& buildDir = "",
                const std::string& outputDir = "",
                std::vector<std::string> sources = {},
                std::vector<std::string> excludes = {},
                bool salign = false,
                bool dryRun = false,
                std::size_t jobs = 1)
  {
    alchemy::cli::CliInputs inputs;
    inputs.paths.buildDir = buildDir;
    inputs.paths.outputDir = outputDir;
    inputs.paths.sourcePatterns = std::move(sources);
    inputs.paths.excludePatterns = std::move(excludes);
    inputs.features.enableSalign = salign;
    inputs.enableDryRun = dryRun;
    inputs.jobs = jobs;
    return inputs;
  }
};

TEST_F(MergeInputsTest, CliWinsWhenNonDefault)
{
  auto cli = makeCliInputs(
      "/cli/build", "/cli/output", {"cli.c"}, {"cli/*"}, true, true, 8);
  auto config = makeCliInputs("/config/build",
                              "/config/output",
                              {"config.c"},
                              {"config/*"},
                              false,
                              false,
                              2);

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, "/cli/build");
  ASSERT_EQ(merged.paths.outputDir, "/cli/output");
  ASSERT_EQ(merged.paths.sourcePatterns.size(), 1);
  ASSERT_EQ(merged.paths.sourcePatterns[0], "cli.c");
  ASSERT_EQ(merged.paths.excludePatterns.size(), 1);
  ASSERT_EQ(merged.paths.excludePatterns[0], "cli/*");
  ASSERT_TRUE(merged.features.enableSalign);
  ASSERT_TRUE(merged.enableDryRun);
  ASSERT_EQ(merged.jobs, 8);
}

TEST_F(MergeInputsTest, ConfigUsedWhenCliIsDefault)
{
  auto cli = makeCliInputs();  // all defaults
  auto config = makeCliInputs("/config/build",
                              "/config/output",
                              {"config.c"},
                              {"config/*"},
                              true,
                              true,
                              4);

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, "/config/build");
  ASSERT_EQ(merged.paths.outputDir, "/config/output");
  ASSERT_EQ(merged.paths.sourcePatterns.size(), 1);
  ASSERT_EQ(merged.paths.sourcePatterns[0], "config.c");
  ASSERT_EQ(merged.paths.excludePatterns.size(), 1);
  ASSERT_EQ(merged.paths.excludePatterns[0], "config/*");
  ASSERT_TRUE(merged.features.enableSalign);
  ASSERT_TRUE(merged.enableDryRun);
  ASSERT_EQ(merged.jobs, 4);
}

TEST_F(MergeInputsTest, MixedCliAndConfigValues)
{
  auto cli = makeCliInputs("/cli/build", "", {"cli.c"}, {}, false, false, 1);
  auto config = makeCliInputs("/config/build",
                              "/config/output",
                              {"config.c"},
                              {"config/*"},
                              true,
                              true,
                              2);

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.paths.buildDir, "/cli/build")
      << "CLI build dir should win (non-empty)";
  ASSERT_EQ(merged.paths.outputDir, "/config/output")
      << "config output dir should be used (CLI empty)";
  ASSERT_EQ(merged.paths.sourcePatterns[0], "cli.c")
      << "CLI sources should win (non-empty)";
  ASSERT_EQ(merged.paths.excludePatterns[0], "config/*")
      << "config excludes should be used (CLI empty)";
  ASSERT_TRUE(merged.features.enableSalign)
      << "config salign should be used (CLI false)";
  ASSERT_TRUE(merged.enableDryRun)
      << "config dry-run should be used (CLI false)";
  ASSERT_EQ(merged.jobs, 2) << "config jobs should be used (CLI is default 1)";
}

TEST_F(MergeInputsTest, JobsConfigAutoDetectAppliedWhenCliDefault)
{
  auto cli = makeCliInputs("", "", {}, {}, false, false, 1);     // CLI default
  auto config = makeCliInputs("", "", {}, {}, false, false, 0);  // auto-detect

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.jobs, 0)
      << "config jobs=0 (auto-detect) should override CLI default of 1";
}

TEST_F(MergeInputsTest, JobsCliExplicitWinsOverConfig)
{
  auto cli = makeCliInputs("", "", {}, {}, false, false, 4);  // explicit
  auto config = makeCliInputs("", "", {}, {}, false, false, 8);

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_EQ(merged.jobs, 4) << "explicit CLI jobs should win over config";
}

TEST_F(MergeInputsTest, BothDefaultsProduceDefaults)
{
  auto cli = makeCliInputs();
  auto config = makeCliInputs();

  auto merged = alchemy::cli::mergeInputs(std::move(cli), std::move(config));

  ASSERT_TRUE(merged.paths.buildDir.empty());
  ASSERT_TRUE(merged.paths.outputDir.empty());
  ASSERT_TRUE(merged.paths.sourcePatterns.empty());
  ASSERT_TRUE(merged.paths.excludePatterns.empty());
  ASSERT_FALSE(merged.features.enableSalign);
  ASSERT_FALSE(merged.enableDryRun);
  ASSERT_EQ(merged.jobs, 1);
}

// ============================================================================
// dumpConfig tests
// ============================================================================

TEST_F(ConfigParserTest, DumpConfigRoundTrip)
{
  alchemy::cli::ParsedOptions options;
  options.buildDir = testDir;
  options.outputDir = testDir;
  options.sourcePatterns = {"src/*.c", "lib/*.c"};
  options.excludePatterns = {"test/*", "vendor/*"};
  options.enableSalign = true;
  options.jobs = 4;
  options.enableDryRun = true;

  alchemy::config::parser::dumpConfig(testDir, options);

  auto result = alchemy::config::parser::loadConfig(testDir / "alchemy.toml");
  ASSERT_TRUE(result.valid()) << "dumped config should round-trip successfully";

  const auto& loaded = result.value();
  ASSERT_EQ(loaded.paths.buildDir, options.buildDir.string());
  ASSERT_EQ(loaded.paths.outputDir, options.outputDir.string());
  ASSERT_EQ(loaded.paths.sourcePatterns.size(), 2);
  ASSERT_EQ(loaded.paths.sourcePatterns[0], "src/*.c");
  ASSERT_EQ(loaded.paths.sourcePatterns[1], "lib/*.c");
  ASSERT_EQ(loaded.paths.excludePatterns.size(), 2);
  ASSERT_EQ(loaded.paths.excludePatterns[0], "test/*");
  ASSERT_EQ(loaded.paths.excludePatterns[1], "vendor/*");
  ASSERT_TRUE(loaded.features.enableSalign);
  ASSERT_EQ(loaded.jobs, 4);
  ASSERT_TRUE(loaded.enableDryRun);
}

TEST_F(ConfigParserTest, DumpConfigHandlesEmptyPatterns)
{
  alchemy::cli::ParsedOptions options;
  options.buildDir = testDir;
  options.outputDir = testDir;
  options.sourcePatterns = {};
  options.excludePatterns = {};
  options.enableSalign = false;
  options.jobs = 1;
  options.enableDryRun = false;

  alchemy::config::parser::dumpConfig(testDir, options);

  auto result = alchemy::config::parser::loadConfig(testDir / "alchemy.toml");
  ASSERT_TRUE(result.valid())
      << "dumped config with empty patterns should round-trip";

  const auto& loaded = result.value();
  ASSERT_TRUE(loaded.paths.sourcePatterns.empty());
  ASSERT_TRUE(loaded.paths.excludePatterns.empty());
  ASSERT_FALSE(loaded.features.enableSalign);
  ASSERT_EQ(loaded.jobs, 1);
  ASSERT_FALSE(loaded.enableDryRun);
}

}  // namespace alchemy::testing
