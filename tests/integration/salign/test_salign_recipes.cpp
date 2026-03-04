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

TEST_F(SalignIntegrationTest, ArrayFieldPreservesCorrectSyntax)
{
  // struct layout before: char(1) + pad(3) + int(4) + uint8_t[3](3) + pad(1)
  // = 12 bytes. after reordering: int(4) + char(1) + uint8_t[3](3) = 8 bytes.
  // the array field must be reordered to verify correct syntax generation
  const std::string TestCode = R"(
#include <stdint.h>

struct WithArrayField {
  uint8_t data[3];
  int value;
  char tag;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "array_field.h", TestCode);
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

  // verify array field has correct C syntax: type name[size], not type[size]
  // name
  ASSERT_NE(Content.find("data[3]"), std::string::npos)
      << "alchemy::testing::integration::array field should preserve "
         "correct C syntax (name[size])";
  ASSERT_EQ(Content.find("uint8_t[3]"), std::string::npos)
      << "alchemy::testing::integration::array brackets should not be "
         "on the type (type[size] name is invalid C)";

  // verify optimization occurred (int should come before uint8_t array)
  ASSERT_LT(Content.find("int value"), Content.find("uint8_t data"))
      << "alchemy::testing::integration::int should be reordered "
         "before uint8_t array";
}

TEST_F(SalignIntegrationTest, ArrayFieldPreservesMacroDimensions)
{
  // array dimensions defined via macros must be preserved in the reordered
  // output — clang's canonical type name resolves them to literals which
  // would break the code if the macro value changes
  const std::string TestCode = R"(
#include <stdint.h>

#define SERIAL_LEN 11

struct WithMacroArray {
  uint8_t serial[SERIAL_LEN];
  int32_t id;
  char tag;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "macro_array.h", TestCode);
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

  // macro name must be preserved, not resolved to literal "11"
  ASSERT_NE(Content.find("[SERIAL_LEN]"), std::string::npos)
      << "array dimension macro should be preserved in reordered output";
  ASSERT_EQ(Content.find("[11]"), std::string::npos)
      << "array dimension macro should NOT be resolved to literal value";

  // verify optimization occurred (int32_t should come before uint8_t array)
  ASSERT_LT(Content.find("int32_t id"), Content.find("uint8_t serial"))
      << "int32_t should be reordered before uint8_t array";
}

TEST_F(SalignIntegrationTest, FileWithIncludeGuardsPreservesPrefix)
{
  const std::string TestCode = R"(
#ifndef MY_HEADER_H
#define MY_HEADER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_NAME_LEN 32

typedef enum {
  STATUS_OK = 0,
  STATUS_ERR = 1
} Status_t;

typedef struct MyStruct {
  char tag;
  double value;
  int count;
} MyStruct_t;

#endif /* MY_HEADER_H */)";
  auto sourceFile =
      utils::createTestFile(tempDir, "guarded_header.h", TestCode);
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

  // verify include guard is preserved
  ASSERT_NE(Content.find("#ifndef MY_HEADER_H"), std::string::npos)
      << "alchemy::testing::integration::include guard should be preserved";
  ASSERT_NE(Content.find("#define MY_HEADER_H"), std::string::npos)
      << "alchemy::testing::integration::include guard define should "
         "be preserved";
  ASSERT_NE(Content.find("#endif"), std::string::npos)
      << "alchemy::testing::integration::endif should be preserved";

  // verify includes are preserved
  ASSERT_NE(Content.find("#include <stdint.h>"), std::string::npos)
      << "alchemy::testing::integration::includes should be preserved";

  // verify defines are preserved
  ASSERT_NE(Content.find("#define MAX_NAME_LEN 32"), std::string::npos)
      << "alchemy::testing::integration::defines should be preserved";

  // verify enum is preserved
  ASSERT_NE(Content.find("typedef enum"), std::string::npos)
      << "alchemy::testing::integration::enum should be preserved";

  // verify typedef struct syntax is preserved
  ASSERT_NE(Content.find("typedef struct MyStruct"), std::string::npos)
      << "alchemy::testing::integration::typedef struct should be preserved";
  ASSERT_NE(Content.find("} MyStruct_t;"), std::string::npos)
      << "alchemy::testing::integration::typedef closing should be preserved";
}

TEST_F(SalignIntegrationTest, BoolFieldPreservesSourceSpelling)
{
  // clang's getAsString() returns "_Bool" for C's bool macro —
  // the replacement text should use the source spelling "bool"
  const std::string TestCode = R"(
#include <stdbool.h>

struct WithBool {
  bool flag;
  double value;
  int count;
};)";
  auto sourceFile = utils::createTestFile(tempDir, "bool_field.h", TestCode);
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

  ASSERT_NE(Content.find("bool flag"), std::string::npos)
      << "alchemy::testing::integration::replacement should use "
         "source spelling 'bool', not '_Bool'";
  ASSERT_EQ(Content.find("_Bool"), std::string::npos)
      << "alchemy::testing::integration::clang canonical '_Bool' "
         "should not appear in output";
}

TEST_F(SalignIntegrationTest, TrailingBlockCommentsPreserved)
{
  // trailing C block comments (/* ... */) should travel with their field
  // during reordering so documentation stays paired with the correct field
  const std::string TestCode = R"(
struct WithBlockComments {
  char tag;       /* tag identifier */
  double value;   /* measured value */
  int count;      /* number of samples */
};)";
  auto sourceFile =
      utils::createTestFile(tempDir, "block_comments.h", TestCode);
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

  // verify reordering occurred (double should come first)
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify trailing block comments travel with their fields
  auto doublePos = Content.find("double value");
  auto measuredPos = Content.find("/* measured value */");
  auto tagIdentPos = Content.find("/* tag identifier */");
  auto charPos = Content.find("char tag");

  ASSERT_NE(measuredPos, std::string::npos)
      << "alchemy::testing::integration::block comment for 'value' "
         "should be preserved";
  ASSERT_NE(tagIdentPos, std::string::npos)
      << "alchemy::testing::integration::block comment for 'tag' "
         "should be preserved";

  // "measured value" comment should appear after "double value"
  ASSERT_LT(doublePos, measuredPos)
      << "alchemy::testing::integration::block comment should follow "
         "its field after reordering";

  // "tag identifier" comment should appear after "char tag"
  ASSERT_LT(charPos, tagIdentPos)
      << "alchemy::testing::integration::block comment should follow "
         "its field after reordering";
}

TEST_F(SalignIntegrationTest, TrailingLineCommentsPreserved)
{
  // trailing C++ line comments (// ...) should travel with their field
  // during reordering so documentation stays paired with the correct field
  const std::string TestCode = R"(
struct WithLineComments {
  char tag;       // tag identifier
  double value;   // measured value
  int count;      // number of samples
};)";
  auto sourceFile = utils::createTestFile(tempDir, "line_comments.h", TestCode);
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

  // verify reordering occurred
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify line comments travel with their fields
  auto doublePos = Content.find("double value");
  auto measuredPos = Content.find("// measured value");
  auto tagIdentPos = Content.find("// tag identifier");
  auto charPos = Content.find("char tag");

  ASSERT_NE(measuredPos, std::string::npos)
      << "alchemy::testing::integration::line comment for 'value' "
         "should be preserved";
  ASSERT_NE(tagIdentPos, std::string::npos)
      << "alchemy::testing::integration::line comment for 'tag' "
         "should be preserved";

  // "measured value" comment should appear after "double value"
  ASSERT_LT(doublePos, measuredPos)
      << "alchemy::testing::integration::line comment should follow "
         "its field after reordering";

  // "tag identifier" comment should appear after "char tag"
  ASSERT_LT(charPos, tagIdentPos)
      << "alchemy::testing::integration::line comment should follow "
         "its field after reordering";
}

TEST_F(SalignIntegrationTest, PrecedingLineCommentsPreserved)
{
  // preceding line comments (// ...) should travel with their field
  // during reordering so documentation stays paired with the correct field
  const std::string TestCode = R"(
struct WithPrecedingComments {
  // tag identifier
  char tag;
  // measured value
  double value;
  // number of samples
  int count;
};)";
  auto sourceFile =
      utils::createTestFile(tempDir, "preceding_line_comments.h", TestCode);
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

  // verify reordering occurred (double should come first)
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify preceding comments travel with their fields
  auto measuredPos = Content.find("// measured value");
  auto doublePos = Content.find("double value");
  auto tagIdentPos = Content.find("// tag identifier");
  auto charPos = Content.find("char tag");

  ASSERT_NE(measuredPos, std::string::npos)
      << "alchemy::testing::integration::preceding comment for 'value' "
         "should be preserved";
  ASSERT_NE(tagIdentPos, std::string::npos)
      << "alchemy::testing::integration::preceding comment for 'tag' "
         "should be preserved";

  // "measured value" comment should appear before "double value"
  ASSERT_LT(measuredPos, doublePos)
      << "alchemy::testing::integration::preceding comment should precede "
         "its field after reordering";

  // "tag identifier" comment should appear before "char tag"
  ASSERT_LT(tagIdentPos, charPos)
      << "alchemy::testing::integration::preceding comment should precede "
         "its field after reordering";
}

TEST_F(SalignIntegrationTest, PrecedingDocCommentPreserved)
{
  // Doxygen-style preceding comments (/// ...) should travel with their field
  const std::string TestCode = R"(
struct WithDocComments {
  /// tag identifier
  char tag;
  /// measured value
  double value;
  /// number of samples
  int count;
};)";
  auto sourceFile =
      utils::createTestFile(tempDir, "preceding_doc_comments.h", TestCode);
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

  // verify reordering occurred
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify doc comments travel with their fields
  auto measuredPos = Content.find("/// measured value");
  auto doublePos = Content.find("double value");
  auto tagIdentPos = Content.find("/// tag identifier");
  auto charPos = Content.find("char tag");

  ASSERT_NE(measuredPos, std::string::npos)
      << "alchemy::testing::integration::doc comment for 'value' "
         "should be preserved";
  ASSERT_NE(tagIdentPos, std::string::npos)
      << "alchemy::testing::integration::doc comment for 'tag' "
         "should be preserved";

  ASSERT_LT(measuredPos, doublePos)
      << "alchemy::testing::integration::doc comment should precede "
         "its field after reordering";

  ASSERT_LT(tagIdentPos, charPos)
      << "alchemy::testing::integration::doc comment should precede "
         "its field after reordering";
}

TEST_F(SalignIntegrationTest, PrecedingBlockCommentPreserved)
{
  // preceding block comments (/* ... */) should travel with their field
  const std::string TestCode = R"(
struct WithPrecedingBlockComments {
  /* tag identifier */
  char tag;
  /* measured value */
  double value;
  /* number of samples */
  int count;
};)";
  auto sourceFile =
      utils::createTestFile(tempDir, "preceding_block_comments.h", TestCode);
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

  // verify reordering occurred
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify preceding block comments travel with their fields
  auto measuredPos = Content.find("/* measured value */");
  auto doublePos = Content.find("double value");
  auto tagIdentPos = Content.find("/* tag identifier */");
  auto charPos = Content.find("char tag");

  ASSERT_NE(measuredPos, std::string::npos)
      << "alchemy::testing::integration::preceding block comment for 'value' "
         "should be preserved";
  ASSERT_NE(tagIdentPos, std::string::npos)
      << "alchemy::testing::integration::preceding block comment for 'tag' "
         "should be preserved";

  ASSERT_LT(measuredPos, doublePos)
      << "alchemy::testing::integration::preceding block comment should "
         "precede its field after reordering";

  ASSERT_LT(tagIdentPos, charPos)
      << "alchemy::testing::integration::preceding block comment should "
         "precede its field after reordering";
}

TEST_F(SalignIntegrationTest, MultiLinePrecedingCommentPreserved)
{
  // multi-line preceding comments should travel with their field
  const std::string TestCode = R"(
struct WithMultiLineComments {
  // tag identifier
  // used for lookup
  char tag;
  // measured value
  // in SI units
  double value;
  // number of samples
  int count;
};)";
  auto sourceFile =
      utils::createTestFile(tempDir, "multiline_preceding.h", TestCode);
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

  // verify reordering occurred
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify both lines of multi-line comments travel together
  ASSERT_NE(Content.find("// measured value"), std::string::npos)
      << "first line of multi-line comment preserved";
  ASSERT_NE(Content.find("// in SI units"), std::string::npos)
      << "second line of multi-line comment preserved";

  // "in SI units" should appear before "double value" (it's part of value's
  // comment)
  ASSERT_LT(Content.find("// in SI units"), Content.find("double value"))
      << "multi-line comment should precede its field after reordering";

  // "used for lookup" should appear before "char tag" (it's part of tag's
  // comment)
  ASSERT_LT(Content.find("// used for lookup"), Content.find("char tag"))
      << "multi-line comment should precede its field after reordering";
}

TEST_F(SalignIntegrationTest, PrecedingAndTrailingCommentsPreserved)
{
  // fields with both preceding and trailing comments should preserve both
  const std::string TestCode = R"(
struct WithBothComments {
  // tag identifier
  char tag;       // single char
  // measured value
  double value;   // floating point
  // number of samples
  int count;      // positive only
};)";
  auto sourceFile = utils::createTestFile(tempDir, "both_comments.h", TestCode);
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

  // verify reordering occurred
  ASSERT_LT(Content.find("double value"), Content.find("char tag"))
      << "alchemy::testing::integration::double should be reordered "
         "before char";

  // verify preceding comments travel with their fields
  ASSERT_LT(Content.find("// measured value"), Content.find("double value"))
      << "preceding comment should precede its field after reordering";
  ASSERT_LT(Content.find("// tag identifier"), Content.find("char tag"))
      << "preceding comment should precede its field after reordering";

  // verify trailing comments travel with their fields
  ASSERT_LT(Content.find("double value"), Content.find("// floating point"))
      << "trailing comment should follow its field after reordering";
  ASSERT_LT(Content.find("char tag"), Content.find("// single char"))
      << "trailing comment should follow its field after reordering";
}

}  // namespace alchemy::testing
