// tests/integration/salign/test_salign_recipes.cpp
// integration tests for salign recipe generation and optimization behavior

// std
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// 3rd party
#include <gtest/gtest.h>

// local
#include "app/app.hpp"
#include "app/core/core.hpp"
#include "data/integration/salign/salign_test_fixture.hpp"
#include "utils.hpp"

namespace alchemy::testing {

TEST_F(SalignIntegrationTest,
       SortedStructsWithNoChangeInSizeShouldNotGenerateRecipes)
{
  const std::string TestCode = R"(
struct NoSizeChange {
  int a;    // 4 bytes at offset 0
  char b;   // 1 byte at offset 4 (3 bytes padding)
  double c; // 8 bytes at offset 8
};  // size: 13 + 3 bytes padding -> total: 16 bytes

// after sorting by alignment:

// double c -> 8 bytes at offset 0
// int a -> 4 bytes at offset 8
// char b -> 1 byte at offset 12 (3 trailing bytes padding)
// size: 13 + 3 bytes padding -> total: 16 bytes
)";

  auto sourceFile = utils::createTestFile(tempDir, "no_change.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::struct with no "
                            "change in size should process successfully";

  const std::string Content = utils::readFile(sourceFile);
  ASSERT_EQ(Content, TestCode) << "alchemy::testing::integration::struct with "
                                  "no change in size should not mutate source";
}

TEST_F(SalignIntegrationTest, PartiallySortedStructShouldGenerateRecipes)
{
  const std::string TestCode = R"(
struct PartiallySorted {
  double a;  // 8 bytes: already sorted
  char b;    // 1 byte
  int c;     // 4 bytes
  char x;    // 1 byte
  char y;    // 1 byte
  char z;    // 1 byte
};  // current size: 24 bytes, optimal size: 16 bytes (double, int, char, char, char, char)
)";
  auto sourceFile = utils::createTestFile(tempDir, "partial.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0);

  const std::string Content = utils::readFile(sourceFile);

  // verify optimization occurred
  ASSERT_NE(Content, TestCode)
      << "alchemy::testing::integration::struct should be optimized";

  // verify specific field order: double should stay first, int should move to
  // position 2
  auto doublePos = Content.find("double a");
  auto intPos = Content.find("int c");
  auto charBPos = Content.find("char b");
  auto charXPos = Content.find("char x");
  auto charYPos = Content.find("char y");
  auto charZPos = Content.find("char z");

  ASSERT_LT(doublePos, intPos)
      << "alchemy::testing::integration::double should stay first";
  ASSERT_LT(intPos, charBPos)
      << "alchemy::testing::integration::int should come before char fields";
  ASSERT_LT(charBPos, charXPos) << "alchemy::testing::integration::char b "
                                   "should come before char x fields";
  ASSERT_LT(charXPos, charYPos) << "alchemy::testing::integration::char x "
                                   "should come before char y fields";
  ASSERT_LT(charYPos, charZPos) << "alchemy::testing::integration::char y "
                                   "should come before char z fields";
}

TEST_F(SalignIntegrationTest, MixedOptimizationAndNonOptimizationInSameFile)
{
  const std::string TestCode = R"(
// struct can be optimized (24 → 16 bytes)
struct BenefitsFromOptimization {
  char a;
  double b;
  int c;
};

// struct reorders but size stays same (16 → 16 bytes)
struct NoBenefit {
  int x;
  char y;
  double z;
};

struct AlreadyOptimal {
  double big;
  int medium;
  char small;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "mixed.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0);

  std::string content = utils::readFile(sourceFile);
  auto extractStructBody =
      [&content](const std::string& structName) -> std::string {
    auto structPos = content.find("struct " + structName);
    if (structPos == std::string::npos)
    {
      return "";
    }

    auto openBrace = content.find('{', structPos);
    if (openBrace == std::string::npos)
    {
      return "";
    }

    auto closeBrace = content.find("};", openBrace);
    if (closeBrace == std::string::npos)
    {
      return "";
    }
    return content.substr(structPos, (closeBrace + 2) - structPos);
  };
  const std::string FirstStruct = extractStructBody("BenefitsFromOptimization");
  auto secondStruct = extractStructBody("NoBenefit");
  const std::string ThirdStruct = extractStructBody("AlreadyOptimal");

  ASSERT_LT(FirstStruct.find("double b"), FirstStruct.find("char a"))
      << "alchemy::testing::integration::BenefitsFromOptimization should be "
         "optimized (double before char)";

  ASSERT_LT(secondStruct.find("int x"), secondStruct.find("double z"))
      << "alchemy::testing::integration::NoBenefit should remain unchanged "
         "(int before double)";

  ASSERT_LT(ThirdStruct.find("double big"), ThirdStruct.find("char small"))
      << "alchemy::testing::integration::AlreadyOptimal should remain "
         "unchanged (double before char)";
}

TEST_F(SalignIntegrationTest, SingleFieldStructGeneratesNoRecipes)
{
  const std::string TestCode = R"(
struct SingleField {
  int onlyField;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "single.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());
  const int ExitCode = appResult.value().exec();

  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::single field struct should process "
         "successfully";

  // single field can't be reordered - file should remain unchanged
  const std::string Content = utils::readFile(sourceFile);
  ASSERT_EQ(Content, TestCode)
      << "alchemy::testing::integration::single field struct should not be "
         "modified";
}

TEST_F(SalignIntegrationTest, SameAlignmentFieldsMaintainOrder)
{
  const std::string TestCode = R"(
struct SameAlign {
  int field1;
  int field2;
  int field3;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "same_align.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());
  const int ExitCode = appResult.value().exec();

  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::same alignment fields should process";

  // all fields have same alignment - no reordering needed
  const std::string Content = utils::readFile(sourceFile);
  ASSERT_EQ(Content, TestCode)
      << "alchemy::testing::integration::same alignment fields should not be "
         "reordered";
}

TEST_F(SalignIntegrationTest, EmptyStructGeneratesNoRecipes)
{
  const std::string TestCode = R"(
struct EmptyStruct {
  // no fields
};)";
  auto sourceFile = utils::createTestFile(tempDir, "empty.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0);

  const std::string Content = utils::readFile(sourceFile);
  ASSERT_EQ(Content, TestCode)
      << "alchemy::testing::integration::empty struct should not be modified";
}

TEST_F(SalignIntegrationTest, CHeaderAndCXXHeaderStructsOptimizeTheSame)
{
  const std::string StructCode = R"(
struct TestStruct {
  char a;
  double b;
  int c;
};)";
  auto buildDir = tempDir / "build";
  std::filesystem::create_directories(buildDir);

  auto cFile = utils::createTestFile(tempDir, "test.h", StructCode);
  utils::createSyntheticCompilationDatabase(buildDir, {cFile.string()});
  auto cApp = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .buildDir = buildDir,  // valid build dir with compile_commands.json
      .outputDir = tempDir,
      .sourceFiles = {cFile.string()},
      .excludePatterns = {},
      .enableSalign = true});
  cApp.value().exec();
  const std::string CResult = utils::readFile(cFile);

  auto cxxFile = utils::createTestFile(tempDir, "test.hpp", StructCode);
  utils::createSyntheticCompilationDatabase(buildDir, {cxxFile.string()});
  auto cxxApp = utils::createAlchemyWithMockOptions(utils::MockCliConfig{
      .buildDir = buildDir,  // valid build dir with compile_commands.json
      .outputDir = tempDir,
      .sourceFiles = {cxxFile.string()},
      .excludePatterns = {},
      .enableSalign = true});
  cxxApp.value().exec();
  const std::string CxxResult = utils::readFile(cxxFile);

  ASSERT_NE(CResult, StructCode)
      << "alchemy::testing::integration::C header file should be optimized";
  ASSERT_NE(CxxResult, StructCode)
      << "alchemy::testing::integration::C++ header file should be optimized";
  ASSERT_EQ(CResult, CxxResult)
      << "alchemy::testing::integration::C and C++ structs should optimize "
         "identically";
  ASSERT_NE(CResult.find("double b"), std::string::npos)
      << "alchemy::testing::integration::double field should exist in C result";
  ASSERT_NE(CResult.find("char a"), std::string::npos)
      << "alchemy::testing::integration::char field should exist in C result";
  ASSERT_LT(CResult.find("double b"), CResult.find("char a"))
      << "alchemy::testing::integration::double field should come before char "
         "field in C result";
}

TEST_F(SalignIntegrationTest, OperationIsIdempotent)
{
  const std::string TestCode = R"(
struct TestStruct {
  char a;
  double b;
  int c;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "idempotent.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  // run first time
  auto appResult1 = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});
  ASSERT_TRUE(appResult1.valid());
  const int ExitCode1 = appResult1.value().exec();
  ASSERT_EQ(ExitCode1, 0);

  const std::string FirstResult = utils::readFile(sourceFile);

  // run second time on already-optimized struct
  auto appResult2 = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});
  ASSERT_TRUE(appResult2.valid());
  const int ExitCode2 = appResult2.value().exec();
  ASSERT_EQ(ExitCode2, 0);

  const std::string SecondResult = utils::readFile(sourceFile);

  // should produce identical results (idempotent)
  ASSERT_EQ(FirstResult, SecondResult)
      << "alchemy::testing::integration::running salign twice should produce "
         "same result";
}

TEST_F(SalignIntegrationTest, AdjacentNonOverlappingRecipesSucceed)
{
  // create struct with multiple fields that will generate adjacent recipes
  const std::string TestCode = R"(
struct AdjacentFields {
  char field1;
  double field2;
  int field3;
  char field4;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "adjacent.h", TestCode);
  utils::createSyntheticCompilationDatabase(tempDir, {sourceFile.string()});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {sourceFile.string()},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid());
  const int ExitCode = appResult.value().exec();

  // should succeed - recipes are adjacent but not overlapping
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::adjacent non-overlapping recipes "
         "should succeed";

  // verify struct was optimized (double should come before char fields)
  const std::string Content = utils::readFile(sourceFile);
  ASSERT_NE(Content, TestCode)
      << "alchemy::testing::integration::struct should be optimized";

  auto doublePos = Content.find("double field2");
  auto char1Pos = Content.find("char field1");
  ASSERT_LT(doublePos, char1Pos)
      << "alchemy::testing::integration::double should come before char";
}

TEST_F(SalignIntegrationTest, EnableDryRunProducesNoTransmutation)
{
  // create test header with unoptimized struct
  const std::string TestCode = R"(
#ifndef TEST_STRUCT_H
#define TEST_STRUCT_H

struct UnoptimizedStruct {
  char small1;        // 1 byte
  double large1;      // 8 bytes (likely needs 7 bytes padding after small1)
  int medium1;        // 4 bytes
  char small2;        // 1 byte (likely needs 3 bytes padding after)
};

#endif // TEST_STRUCT_H)";
  utils::createTestFile(tempDir, "test_struct.h", TestCode);

  // create alchemy app with mocked --salign flag (avoids LLVM CLI global state)
  std::string headerPath = (tempDir / "test_struct.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true,
                           .enableDryRun = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed: "
      << appResult.error();

  // execute the complete pipeline (discovery → parsing → analysis →
  // transmutation)
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::salign pipeline "
                            "should complete successfully";

  // verify source file was analyzed but no recipes actually applied
  const std::ifstream TransmutedFile(headerPath);
  ASSERT_TRUE(TransmutedFile.good())
      << "alchemy::testing::integration::transmuted file should exist";

  std::stringstream transmutedContent;
  transmutedContent << TransmutedFile.rdbuf();
  const std::string TransmutedStr = transmutedContent.str();

  // verify original file content has not changed
  ASSERT_EQ(TransmutedStr, TestCode)
      << "alchemy::testing::integration::struct fields should be reordered for "
         "optimal alignment";
}

}  // namespace alchemy::testing
