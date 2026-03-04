// tests/performance/test_salign_complexity.cpp
// performance tests for struct complexity: field count variation

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

class SalignComplexityPerformanceTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir = std::filesystem::temp_directory_path() /
              "alchemy_salign_complexity_test" /
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(tempDir);
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

  // helper: create unoptimized struct with N fields
  // pattern: alternating small/large types to force padding
  std::string
  createUnoptimizedStruct(const std::string& structName, std::size_t fieldCount)
  {
    std::ostringstream oss;
    oss << "struct " << structName << " {\n";

    for (std::size_t i = 0; i < fieldCount; ++i)
    {
      // alternate between char (1 byte) and double (8 bytes) to force maximum
      // padding
      if (i % 2 == 0)
      {
        oss << "    char field_" << i << ";        // 1 byte\n";
      }
      else
      {
        oss << "    double field_" << i
            << ";      // 8 bytes (padding before this)\n";
      }
    }

    oss << "};\n\n";
    return oss.str();
  }

  // helper: create file with structs of specified field count
  void
  createFileWithComplexStructs(const std::string& filename,
                               std::size_t structCount,
                               std::size_t FieldsPerStruct)
  {
    // Build entire file content at once
    std::ostringstream content;

    // Header guards
    std::string guardName = filename;
    std::replace(guardName.begin(), guardName.end(), '.', '_');
    std::transform(
        guardName.begin(), guardName.end(), guardName.begin(), ::toupper);

    content << "#ifndef " << guardName << "\n";
    content << "#define " << guardName << "\n\n";

    // All structs
    // strip .h extension from filename for valid struct names
    const std::string BaseName = filename.substr(0, filename.find_last_of('.'));

    for (std::size_t idx = 0; idx < structCount; ++idx)
    {
      const std::string StructName =
          "UnoptimizedStruct_" + BaseName + "_" + std::to_string(idx);
      content << createUnoptimizedStruct(StructName, FieldsPerStruct);
    }

    // Closing header guard
    content << "#endif // " << guardName << "\n";

    // Write once
    utils::createTestFile(tempDir, filename, content.str());
  }

  std::filesystem::path tempDir;
};

// test: simple structs with minimal fields
// hypothesis: fewer fields = less analysis overhead
TEST_F(SalignComplexityPerformanceTest, SimpleStructs_2Fields)
{
  const std::size_t FileCount = 5;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 2;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithComplexStructs(Filename, StructsPerFile, FieldsPerStruct);
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
  std::cout << "[COMPLEXITY SIMPLE] 5 files × 100 structs × 2 fields: "
            << duration.count() << "ms" << '\n';
}

// test: baseline complexity (4 fields - matches original baseline)
TEST_F(SalignComplexityPerformanceTest, BaselineStructs_4Fields)
{
  const std::size_t FileCount = 5;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 4;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithComplexStructs(Filename, StructsPerFile, FieldsPerStruct);
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
  std::cout << "[COMPLEXITY BASELINE] 5 files × 100 structs × 4 fields: "
            << duration.count() << "ms" << '\n';
}

// test: moderately complex structs
TEST_F(SalignComplexityPerformanceTest, ModerateStructs_8Fields)
{
  const std::size_t FileCount = 5;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 8;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithComplexStructs(Filename, StructsPerFile, FieldsPerStruct);
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
  std::cout << "[COMPLEXITY MODERATE] 5 files × 100 structs × 8 fields: "
            << duration.count() << "ms" << '\n';
}

// test: complex structs (realistic embedded device structure)
TEST_F(SalignComplexityPerformanceTest, ComplexStructs_16Fields)
{
  const std::size_t FileCount = 5;
  const std::size_t StructsPerFile = 100;
  const std::size_t FieldsPerStruct = 16;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "TestStruct_" + std::to_string(i) + ".h";
    createFileWithComplexStructs(Filename, StructsPerFile, FieldsPerStruct);
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
  std::cout << "[COMPLEXITY COMPLEX] 5 files × 100 structs × 16 fields: "
            << duration.count() << "ms" << '\n';
}

}  // namespace alchemy::testing
