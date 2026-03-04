// tests/performance/test_salign_parsing.cpp
// performance tests for parsing overhead: file count vs struct count

// std
#include <cctype>
#include <chrono>
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "utils.hpp"

namespace alchemy::testing {

class SalignParsingPerformanceTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_salign_parsing_test");
  }

  void
  TearDown() override
  {
    // cleanup temporary directory
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  // helper: create a file with N unoptimized structs
  void
  createFileWithStructs(const std::string& filename, std::size_t structCount)
  {
    // Build entire file content at once
    std::ostringstream content;

    // Header guards
    std::string guardName = filename;
    std::replace(std::begin(guardName), std::end(guardName), '.', '_');
    std::transform(std::begin(guardName),
                   std::end(guardName),
                   std::begin(guardName),
                   ::toupper);

    content << "#ifndef " << guardName << "\n";
    content << "#define " << guardName << "\n\n";

    // All structs
    // strip .h extension from filename for valid struct names
    const std::string BaseName = filename.substr(0, filename.find_last_of('.'));

    for (std::size_t idx = 0; idx < structCount; ++idx)
    {
      content << "struct UnoptimizedStruct_" << BaseName << "_" << idx
              << " {\n";
      content << "    char small1;        // 1 byte\n";
      content
          << "    double large1;      // 8 bytes (likely needs 7 bytes padding "
             "after small1)\n";
      content << "    int medium1;        // 4 bytes\n";
      content
          << "    char small2;        // 1 byte (likely needs 3 bytes padding "
             "after)\n";
      content << "};\n\n";
    }

    // Closing header guard
    content << "#endif // " << guardName << "\n";

    // Write once
    utils::createTestFile(tempDir, filename, content.str());
  }

  std::filesystem::path tempDir;
};

// baseline: 5 files × 100 structs = 500 total
// this matches the original baseline test for comparison
TEST_F(SalignParsingPerformanceTest, Baseline_5Files_100StructsEach)
{
  const std::size_t FileCount = 5;
  const std::size_t StructsPerFile = 100;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithStructs(Filename, StructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[PARSING BASELINE] 5 files × 100 structs = 500 total: "
            << duration.count() << "ms" << '\n';
}

// test: many files with few structs
// hypothesis: if discovery/glob matching dominates, this will be slower than
// baseline
TEST_F(SalignParsingPerformanceTest, ManyFiles_50Files_10StructsEach)
{
  const std::size_t FileCount = 50;
  const std::size_t StructsPerFile = 10;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithStructs(Filename, StructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[PARSING MANY_FILES] 50 files × 10 structs = 500 total: "
            << duration.count() << "ms" << '\n';
}

// test: few files with many structs
// hypothesis: if parsing/AST traversal dominates, this will be similar to
// baseline
TEST_F(SalignParsingPerformanceTest, FewFiles_1File_500Structs)
{
  const std::size_t FileCount = 1;
  const std::size_t StructsPerFile = 500;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithStructs(Filename, StructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[PARSING FEW_FILES] 1 file × 500 structs = 500 total: "
            << duration.count() << "ms" << '\n';
}

}  // namespace alchemy::testing
