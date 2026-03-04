// tests/performance/test_salign_stress.cpp
// stress tests for struct alignment -> extreme cases to test scalability limits

// std
#include <cctype>
#include <chrono>
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "utils.hpp"

namespace alchemy::testing {

class SalignStressTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_salign_stress_test");
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

  // helper: create unoptimized struct with variable field count
  std::string
  createUnoptimizedStruct(const std::string& structName, std::size_t fieldCount)
  {
    std::ostringstream oss;
    oss << "struct " << structName << " {\n";

    for (std::size_t i = 0; i < fieldCount; ++i)
    {
      // alternate to force padding
      if (i % 2 == 0)
      {
        oss << "    char field_" << i << ";\n";
      }
      else
      {
        oss << "    double field_" << i << ";\n";
      }
    }

    oss << "};\n\n";
    return oss.str();
  }

  void
  createStressFile(const std::string& filename,
                   std::size_t structCount,
                   std::size_t FieldsPerStruct)
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
      const std::string StructName =
          "StressStruct_" + BaseName + "_" + std::to_string(idx);
      content << createUnoptimizedStruct(StructName, FieldsPerStruct);
    }

    // Closing header guard
    content << "#endif // " << guardName << "\n";

    // Write once
    utils::createTestFile(tempDir, filename, content.str());
  }

  std::filesystem::path tempDir;
  std::mt19937 rng{42};  // NOLINT(cert-msc32-c,cert-msc51-cpp) - intentional
                         // for reproducible performance tests
};

// stress test: very large single file
// tests parser/AST traversal limits without file discovery overhead
TEST_F(SalignStressTest, SingleFile_10000Structs)
{
  const std::size_t FileCount = 1;
  const std::size_t StructsPerFile = 10000;
  const std::size_t FieldsPerStruct = 4;

  std::cout << "[STRESS] Creating 1 file with 10,000 structs..." << '\n';

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "huge_file_" + std::to_string(i) + ".h";
    createStressFile(Filename, StructsPerFile, FieldsPerStruct);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign,
                           .jobs = std::thread::hardware_concurrency()});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[STRESS SINGLE_FILE] 1 file × 10,000 structs = 10,000 total: "
            << duration.count() << "ms" << '\n';
}

// stress test: many files with moderate structs
// tests file discovery/ClangTool overhead at scale
TEST_F(SalignStressTest, ManyFiles_200Files_100StructsEach)
{
  const std::size_t FileCount = 200;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 4;

  std::cout << "[STRESS] Creating 200 files with 100 structs each..." << '\n';

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "stress_file_" + std::to_string(i) + ".h";
    createStressFile(Filename, StructsPerFile, FieldsPerStruct);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign,
                           .jobs = std::thread::hardware_concurrency()});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[STRESS MANY_FILES] 200 files × 100 structs = 20,000 total: "
            << duration.count() << "ms" << '\n';
}

// stress test: extreme case - massive codebase
// tests absolute limits of current implementation
TEST_F(SalignStressTest, ExtremeCase_500Files_100StructsEach)
{
  const std::size_t FileCount = 500;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 4;

  std::cout << "[STRESS] Creating 500 files with 100 structs each..." << '\n';
  std::cout << "[STRESS] This may take several minutes..." << '\n';

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "extreme_file_" + std::to_string(i) + ".h";
    createStressFile(Filename, StructsPerFile, FieldsPerStruct);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {std::move(std::filesystem::path(
                               tempDir.string() + "/*.h"))},
                           .enableSalign = EnableSalign,
                           .jobs = std::thread::hardware_concurrency()});

  ASSERT_TRUE(appResult.valid())
      << "app creation should succeed: " << appResult.error();

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "salign pipeline should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "[STRESS EXTREME] 500 files × 100 structs = 50,000 total: "
            << duration.count() << "ms ("
            << (static_cast<double>(duration.count()) / 1000.0) << "s)" << '\n';
}

}  // namespace alchemy::testing
