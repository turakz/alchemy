// tests/performance/test_salign.cpp
// performance test for struct alignment flag `-salign`

// std
#include <chrono>
#include <cstddef>

#include <filesystem>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

// 3rd party
#include <gtest/gtest.h>

// local
#include "utils.hpp"

namespace alchemy::testing {

class SalignPerformanceTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir =
        alchemy::testing::utils::createTempTestDirectory("alchemy_salign_test");
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

  void
  setupTestData(const std::string& filename)
  {
    // Build entire file content at once
    std::ostringstream content;

    // Header guards
    content << "#ifndef TEST_STRUCT_H\n";
    content << "#define TEST_STRUCT_H\n\n";

    // All 100 structs
    for (std::size_t idx = 0; idx < 100; ++idx)
    {
      content << "struct UnoptimizedStruct_" << idx << " {\n";
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
    content << "#endif // TEST_STRUCT_H\n";

    // Write once
    utils::createTestFile(tempDir, filename, content.str());
  }

  std::filesystem::path tempDir;
};

TEST_F(SalignPerformanceTest, TimeForFiveFilesEachWithOneHundredStructs)
{
  const std::string FileOne = "TestStruct_1.h";
  setupTestData(FileOne);
  const std::string FileTwo = "TestStruct_2.h";
  setupTestData(FileTwo);
  const std::string FileThree = "TestStruct_3.h";
  setupTestData(FileThree);
  const std::string FileFour = "TestStruct_4.h";
  setupTestData(FileFour);
  const std::string FileFive = "TestStruct_5.h";
  setupTestData(FileFive);

  const bool EnableSalign = true;
  utils::createSyntheticCompilationDatabase(tempDir);

  auto start = std::chrono::high_resolution_clock::now();

  auto appResult = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .buildDir = tempDir,
      .outputDir = tempDir,
      .sourceFiles = {std::move(
          std::filesystem::path(tempDir.string() + "/*.h"))},
      .enableSalign = EnableSalign,
      .jobs = static_cast<std::size_t>(std::thread::hardware_concurrency())});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::performance::app creation should succeed: "
      << appResult.error();

  // execute the complete pipeline (discovery → parsing → analysis →
  // transmutation)
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::performance::salign pipeline "
                            "should complete successfully";

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "alchemy ran for a duration of " << duration.count() << "ms"
            << '\n';
}

}  // namespace alchemy::testing
