// tests/performance/test_salign_realistic.cpp
// performance tests for realistic embedded and small project scenarios

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
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "utils.hpp"

namespace alchemy::testing {

class SalignRealisticPerformanceTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_salign_realistic_test");
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
      // alternate between char and double to force padding (unoptimized)
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

  // helper: create file with realistic variable struct count
  void
  createRealisticFile(const std::string& filename,
                      std::size_t minStructs,
                      std::size_t maxStructs)
  {
    // Build entire file content at once
    std::ostringstream content;

    // generate random struct count within range
    std::uniform_int_distribution<std::size_t> structDist(minStructs,
                                                          maxStructs);
    const std::size_t StructCount = structDist(rng);

    // variable field count (2-10 fields, realistic for embedded)
    std::uniform_int_distribution<std::size_t> fieldDist(2, 10);

    // Header guards
    std::string guardName = filename;
    std::replace(std::begin(guardName), std::end(guardName), '.', '_');
    std::transform(std::begin(guardName),
                   std::end(guardName),
                   std::begin(guardName),
                   ::toupper);

    content << "#ifndef " << guardName << "\n";
    content << "#define " << guardName << "\n\n";

    // All structs with variable field counts
    // strip .h extension from filename for valid struct names
    const std::string BaseName = filename.substr(0, filename.find_last_of('.'));

    for (std::size_t idx = 0; idx < StructCount; ++idx)
    {
      const std::size_t FieldCount = fieldDist(rng);
      const std::string StructName =
          "Struct_" + BaseName + "_" + std::to_string(idx);
      content << createUnoptimizedStruct(StructName, FieldCount);
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

// test: embedded firmware project
// typical: 20-30 header files, 5-30 structs per file, ~300-500 total structs
TEST_F(SalignRealisticPerformanceTest, EmbeddedProject_20Files_VaryingStructs)
{
  const std::size_t FileCount = 20;
  const std::size_t MinStructsPerFile = 5;
  const std::size_t MaxStructsPerFile = 30;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "module_" + std::to_string(i) + ".h";
    createRealisticFile(Filename, MinStructsPerFile, MaxStructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
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
  std::cout
      << "[REALISTIC EMBEDDED] 20 files, varying structs (5-30 per file): "
      << duration.count() << "ms" << '\n';
}

// test: small to medium project
// typical: 50 header files, 10-50 structs per file, ~1000-2000 total structs
TEST_F(SalignRealisticPerformanceTest,
       SmallToMediumProject_50Files_VaryingStructs)
{
  const std::size_t FileCount = 50;
  const std::size_t MinStructsPerFile = 10;
  const std::size_t MaxStructsPerFile = 50;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "component_" + std::to_string(i) + ".h";
    createRealisticFile(Filename, MinStructsPerFile, MaxStructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
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
  std::cout
      << "[REALISTIC SMALL_MEDIUM] 50 files, varying structs (10-50 per file): "
      << duration.count() << "ms" << '\n';
}

// test: larger embedded project (upper bound for "reasonable" testing)
// typical: 100 header files, 20-100 structs per file, ~3000-7000 total structs
TEST_F(SalignRealisticPerformanceTest, LargerProject_100Files_VaryingStructs)
{
  const std::size_t FileCount = 100;
  const std::size_t MinStructsPerFile = 20;
  const std::size_t MaxStructsPerFile = 100;

  for (std::size_t i = 0; i < FileCount; ++i)
  {
    const std::string Filename = "subsystem_" + std::to_string(i) + ".h";
    createRealisticFile(Filename, MinStructsPerFile, MaxStructsPerFile);
  }

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.rootDir = tempDir,
                           .buildDir = tempDir,
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
  std::cout
      << "[REALISTIC LARGER] 100 files, varying structs (20-100 per file): "
      << duration.count() << "ms" << '\n';
}

}  // namespace alchemy::testing
