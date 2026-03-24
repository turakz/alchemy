// tests/unit/test_discovery.cpp

// std
#include <filesystem>

// 3rd party
#include <gtest/gtest.h>

// local
#include "files/discovery.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class DiscoveryTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory structure
    testDir = std::filesystem::temp_directory_path() / "alchemy_discovery_test";
    std::filesystem::create_directories(testDir);

    srcDir = testDir / "src";
    incDir = testDir / "inc";
    testsDir = testDir / "tests";

    std::filesystem::create_directories(srcDir);
    std::filesystem::create_directories(incDir);
    std::filesystem::create_directories(testsDir);

    // create test files
    utils::createTestFile(srcDir, "main.h");
    utils::createTestFile(srcDir, "parser.h");
    utils::createTestFile(srcDir, "app.cpp");  // non-.h file
    utils::createTestFile(incDir, "core.h");
    utils::createTestFile(incDir, "cli.h");
    utils::createTestFile(testsDir, "test_main.h");
    utils::createTestFile(testsDir, "helper.h");

    // save original directory for relative path tests
    originalDir = std::filesystem::current_path();
  }

  void
  TearDown() override
  {
    // restore original directory
    std::filesystem::current_path(originalDir);

    // cleanup test directory
    if (std::filesystem::exists(testDir))
    {
      std::filesystem::remove_all(testDir);
    }
  }

  std::filesystem::path testDir;
  std::filesystem::path srcDir;
  std::filesystem::path incDir;
  std::filesystem::path testsDir;
  std::filesystem::path originalDir;
};

// test basic functionality
TEST_F(DiscoveryTest, DiscoverFilesHandlesEmptyPatterns)
{
  auto result = discovery::discoverFiles({}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with empty patterns";

  const auto& discoveryResult = result.value();
  ASSERT_TRUE(discoveryResult.sourceFiles.empty());
  ASSERT_TRUE(discoveryResult.excludedFiles.empty());
}

TEST_F(DiscoveryTest, DiscoverFilesFindsMatchingFiles)
{
  // test direct file specification
  const std::string MainHeaderPath = (srcDir / "main.h").string();
  auto result = discovery::discoverFiles({MainHeaderPath}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with direct file path";

  const auto& discoveryResult = result.value();
  ASSERT_EQ(discoveryResult.sourceFiles.size(), 1);
  ASSERT_EQ(discoveryResult.sourceFiles[0], srcDir / "main.h");
  ASSERT_TRUE(discoveryResult.excludedFiles.empty());
}

TEST_F(DiscoveryTest, DiscoverFilesRespectsExcludePatterns)
{
  // include all .h files in src but exclude specific one
  const std::string SrcPattern = srcDir.string() + "/*";
  const std::string ExcludeFile = (srcDir / "parser.h").string();

  auto result = discovery::discoverFiles({SrcPattern}, {ExcludeFile});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with exclude patterns";

  const auto& discoveryResult = result.value();

  // should find main.h but not parser.h
  bool foundMain = false;
  bool foundParser = false;
  for (const auto& file : discoveryResult.sourceFiles)
  {
    if (file.filename() == "main.h")
    {
      foundMain = true;
    }
    if (file.filename() == "parser.h")
    {
      foundParser = true;
    }
  }

  ASSERT_TRUE(foundMain) << "alchemy::testing::unit:should find main.h";
  ASSERT_FALSE(foundParser) << "alchemy::testing::unit:should exclude parser.h";

  // verify excluded file is in excludedFiles list
  ASSERT_EQ(discoveryResult.excludedFiles.size(), 1);
  ASSERT_EQ(discoveryResult.excludedFiles[0], srcDir / "parser.h");
}

TEST_F(DiscoveryTest, DiscoverFilesHandlesNonExistentDirectories)
{
  const std::string NonExistentPattern = "/this/path/does/not/exist/*";

  auto result = discovery::discoverFiles({NonExistentPattern}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:should handle "
                                 "non-existent directories gracefully";

  const auto& discoveryResult = result.value();
  ASSERT_TRUE(discoveryResult.sourceFiles.empty())
      << "alchemy::testing::unit:should return empty results for "
         "non-existent directories";
  ASSERT_TRUE(discoveryResult.excludedFiles.empty())
      << "alchemy::testing::unit:should return empty results for "
         "non-existent directories";
}

TEST_F(DiscoveryTest, DiscoverFilesReturnsValidPaths)
{
  const std::string IncPattern = incDir.string() + "/*";
  auto result = discovery::discoverFiles({IncPattern}, {});

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit:discoverFiles should succeed";

  const auto& discoveryResult = result.value();

  // verify all returned paths are valid and exist
  for (const auto& file : discoveryResult.sourceFiles)
  {
    ASSERT_TRUE(std::filesystem::exists(file))
        << "alchemy::testing::unit:returned file should exist: " << file;
    ASSERT_TRUE(std::filesystem::is_regular_file(file))
        << "alchemy::testing::unit:returned path should be a regular file: "
        << file;
    ASSERT_EQ(file.extension(), ".h")
        << "alchemy::testing::unit:returned file should have .h extension: "
        << file;
  }
}

// test wildcard pattern matching
TEST_F(DiscoveryTest, HandlesWildcardPatterns)
{
  // test *.h pattern to match all .h files in directory
  const std::string WildcardPattern = srcDir.string() + "/*.h";
  auto result = discovery::discoverFiles({WildcardPattern}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with wildcard pattern";

  const auto& discoveryResult = result.value();

  // should find main.h and parser.h but not app.cpp
  ASSERT_EQ(discoveryResult.sourceFiles.size(), 2);

  bool foundMain = false;
  bool foundParser = false;
  for (const auto& file : discoveryResult.sourceFiles)
  {
    if (file.filename() == "main.h")
    {
      foundMain = true;
    }
    if (file.filename() == "parser.h")
    {
      foundParser = true;
    }
    ASSERT_EQ(file.extension(), ".h")
        << "alchemy::testing::unit:wildcard should only match .h files";
  }

  ASSERT_TRUE(foundMain) << "alchemy::testing::unit:should find main.h";
  ASSERT_TRUE(foundParser) << "alchemy::testing::unit:should find parser.h";
}

// test recursive glob pattern
TEST_F(DiscoveryTest, HandlesRecursiveGlobPattern)
{
  // test **/*.h pattern to recursively find all .h files
  const std::string RecursivePattern = testDir.string() + "/**/*.h";
  auto result = discovery::discoverFiles({RecursivePattern}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with recursive pattern";

  const auto& discoveryResult = result.value();

  // should find .h files from src/, inc/, and tests/ directories
  ASSERT_GE(discoveryResult.sourceFiles.size(), 6)
      << "alchemy::testing::unit:should find files from multiple directories";

  // verify files from different directories were found
  bool foundInSrc = false;
  bool foundInInc = false;
  bool foundInTests = false;

  for (const auto& file : discoveryResult.sourceFiles)
  {
    const std::string PathStr = file.string();
    if (PathStr.find("/src/") != std::string::npos)
    {
      foundInSrc = true;
    }
    if (PathStr.find("/inc/") != std::string::npos)
    {
      foundInInc = true;
    }
    if (PathStr.find("/tests/") != std::string::npos)
    {
      foundInTests = true;
    }
  }

  ASSERT_TRUE(foundInSrc) << "alchemy::testing::unit:should find files in src/";
  ASSERT_TRUE(foundInInc) << "alchemy::testing::unit:should find files in inc/";
  ASSERT_TRUE(foundInTests)
      << "alchemy::testing::unit:should find files in tests/";
}

// test multiple wildcard patterns
TEST_F(DiscoveryTest, HandlesMultipleWildcardPatterns)
{
  // test multiple patterns: src/*.h and inc/*.h
  const std::string SrcPattern = srcDir.string() + "/*.h";
  const std::string IncPattern = incDir.string() + "/*.h";

  auto result = discovery::discoverFiles({SrcPattern, IncPattern}, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit:discoverFiles "
                                 "should succeed with multiple patterns";

  const auto& discoveryResult = result.value();

  // should find files from both src/ and inc/
  ASSERT_GE(discoveryResult.sourceFiles.size(), 4)
      << "alchemy::testing::unit:should find files from both directories";

  bool foundInSrc = false;
  bool foundInInc = false;

  for (const auto& file : discoveryResult.sourceFiles)
  {
    const std::string PathStr = file.string();
    if (PathStr.find("/src/") != std::string::npos)
    {
      foundInSrc = true;
    }
    if (PathStr.find("/inc/") != std::string::npos)
    {
      foundInInc = true;
    }
  }

  ASSERT_TRUE(foundInSrc) << "alchemy::testing::unit:should find files in src/";
  ASSERT_TRUE(foundInInc) << "alchemy::testing::unit:should find files in inc/";
}

}  // namespace alchemy::testing
