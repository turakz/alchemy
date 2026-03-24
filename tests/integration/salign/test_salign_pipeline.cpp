// tests/integration/salign/test_salign_pipeline.cpp
// integration tests for salign pipeline execution

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

// test complete salign pipeline with simple struct
TEST_F(SalignIntegrationTest, ProcessesSimpleCHeaderUnoptimizedStruct)
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

#endif // TEST_STRUCT_H
)";

  utils::createTestFile(tempDir, "test_struct.h", TestCode);

  // create alchemy app with mocked --salign flag (avoids LLVM CLI global state)
  std::string headerPath = (tempDir / "test_struct.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed: "
      << appResult.error();

  // execute the complete pipeline (discovery → parsing → analysis →
  // transmutation)
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::salign c header pipeline "
         "should complete successfully";

  // verify source file was transmuted with optimized field order
  const std::ifstream TransmutedFile(headerPath);
  ASSERT_TRUE(TransmutedFile.good())
      << "alchemy::testing::integration::transmuted file should exist";

  std::stringstream transmutedContent;
  transmutedContent << TransmutedFile.rdbuf();
  const std::string TransmutedStr = transmutedContent.str();

  // verify struct fields are now in optimal order (descending alignment:
  // double, int, char, char)
  auto doublePos = TransmutedStr.find("double large1");
  auto intPos = TransmutedStr.find("int medium1");
  auto char1Pos = TransmutedStr.find("char small1");
  auto char2Pos = TransmutedStr.find("char small2");

  ASSERT_NE(doublePos, std::string::npos)
      << "alchemy::testing::integration::double field should exist";
  ASSERT_NE(intPos, std::string::npos)
      << "alchemy::testing::integration::int field should exist";
  ASSERT_NE(char1Pos, std::string::npos)
      << "alchemy::testing::integration::char small1 field should exist";
  ASSERT_NE(char2Pos, std::string::npos)
      << "alchemy::testing::integration::char small2 field should exist";

  ASSERT_LT(doublePos, intPos) << "alchemy::testing::integration::double "
                                  "should come before int (largest "
                                  "alignment first)";
  ASSERT_LT(intPos, char1Pos)
      << "alchemy::testing::integration::int should come before first char";
  ASSERT_LT(char1Pos, char2Pos)
      << "alchemy::testing::integration::char fields should maintain relative "
         "order";
}

TEST_F(SalignIntegrationTest, ProcessesSimpleCXXHeaderUnoptimizedStruct)
{
  // create test header with unoptimized struct
  const std::string TestCode = R"(
#ifndef TEST_STRUCT_HPP
#define TEST_STRUCT_HPP

struct UnoptimizedStruct {
  char small1;        // 1 byte
  double large1;      // 8 bytes (likely needs 7 bytes padding after small1)
  int medium1;        // 4 bytes
  char small2;        // 1 byte (likely needs 3 bytes padding after)
};

#endif // TEST_STRUCT_HPP
)";

  utils::createTestFile(tempDir, "test_struct.hpp", TestCode);

  // create alchemy app with mocked --salign flag (avoids LLVM CLI global state)
  std::string headerPath = (tempDir / "test_struct.hpp").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed: "
      << appResult.error();

  // execute the complete pipeline (discovery → parsing → analysis →
  // transmutation)
  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::salign cxx header pipeline "
         "should complete successfully";

  // verify source file was transmuted with optimized field order
  const std::ifstream TransmutedFile(headerPath);
  ASSERT_TRUE(TransmutedFile.good())
      << "alchemy::testing::integration::transmuted file should exist";

  std::stringstream transmutedContent;
  transmutedContent << TransmutedFile.rdbuf();
  const std::string TransmutedStr = transmutedContent.str();

  // verify struct fields are now in optimal order (descending alignment:
  // double, int, char, char)
  auto doublePos = TransmutedStr.find("double large1");
  auto intPos = TransmutedStr.find("int medium1");
  auto char1Pos = TransmutedStr.find("char small1");
  auto char2Pos = TransmutedStr.find("char small2");

  ASSERT_NE(doublePos, std::string::npos)
      << "alchemy::testing::integration::double field should exist";
  ASSERT_NE(intPos, std::string::npos)
      << "alchemy::testing::integration::int field should exist";
  ASSERT_NE(char1Pos, std::string::npos)
      << "alchemy::testing::integration::char small1 field should exist";
  ASSERT_NE(char2Pos, std::string::npos)
      << "alchemy::testing::integration::char small2 field should exist";

  ASSERT_LT(doublePos, intPos) << "alchemy::testing::integration::double "
                                  "should come before int (largest "
                                  "alignment first)";
  ASSERT_LT(intPos, char1Pos)
      << "alchemy::testing::integration::int should come before first char";
  ASSERT_LT(char1Pos, char2Pos)
      << "alchemy::testing::integration::char fields should maintain relative "
         "order";
}

// test with multiple structs in same file
TEST_F(SalignIntegrationTest, ProcessesMultipleStructsInCHeaderFile)
{
  const std::string TestCode = R"(
struct FirstStruct {
  char a;
  double b;
  int c;
};

struct SecondStruct {
  short x;
  long long y;
  char z;
};

struct AlreadyOptimal {
  double big;
  int medium;
  char small;
};
)";

  utils::createTestFile(tempDir, "multi_struct.h", TestCode);
  // create alchemy app with mocked --salign flag (avoids LLVM CLI global state)
  std::string headerPath = (tempDir / "multi_struct.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid()) << "alchemy::testing::integration::app should "
                                    "handle multiple structs in c header file";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::multiple struct "
                            "c header processing should succeed";

  // verify file was transmuted with optimized structs
  const std::ifstream TransmutedFile(headerPath);
  ASSERT_TRUE(TransmutedFile.good());

  std::stringstream transmutedContent;
  transmutedContent << TransmutedFile.rdbuf();
  const std::string TransmutedStr = transmutedContent.str();

  // FirstStruct and SecondStruct should be optimized, AlreadyOptimal unchanged
  const std::string ExpectedContent = R"(
struct FirstStruct {
  double b;
  int c;
  char a;
};

struct SecondStruct {
  long long y;
  short x;
  char z;
};

struct AlreadyOptimal {
  double big;
  int medium;
  char small;
};
)";

  ASSERT_EQ(TransmutedStr, ExpectedContent)
      << "alchemy::testing::integration::unoptimized structs in c header "
         "should be "
         "reordered, optimal struct unchanged";
}

// test with multiple header files
TEST_F(SalignIntegrationTest, ProcessesMultipleHeaderFiles)
{
  // create first header
  // char, long, int -> will benefit from reordering (size decreases from 24 to
  // 16)
  utils::createTestFile(tempDir, "struct1.h", R"(
struct FileOneStruct {
  char field1;
  long field2;
  int field3;
};
)");

  // create second header
  utils::createTestFile(tempDir, "struct2.h", R"(
struct FileTwoStruct {
  char field1;
  long field2;
  int field3;
};
)");

  std::string header1 = (tempDir / "struct1.h").string();
  std::string header2 = (tempDir / "struct2.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {header1, header2});

  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {header1, header2},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app should handle multiple files";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::multiple file "
                            "processing should succeed";

  // verify both files were transmuted
  const std::ifstream File1(header1);
  ASSERT_TRUE(File1.good());
  std::stringstream content1;
  content1 << File1.rdbuf();

  const std::ifstream File2(header2);
  ASSERT_TRUE(File2.good());
  std::stringstream content2;
  content2 << File2.rdbuf();

  // FileOneStruct should be optimized (long, int, char)
  const std::string ExpectedContent1 = R"(
struct FileOneStruct {
  long field2;
  int field3;
  char field1;
};
)";

  // FileTwoStruct should be optimized (long, int, char sorted by alignment)
  const std::string ExpectedContent2 = R"(
struct FileTwoStruct {
  long field2;
  int field3;
  char field1;
};
)";

  ASSERT_EQ(content1.str(), ExpectedContent1)
      << "alchemy::testing::integration::struct1.h should be optimized";
  ASSERT_EQ(content2.str(), ExpectedContent2)
      << "alchemy::testing::integration::struct2.h should be optimized";
}

// test with glob pattern
TEST_F(SalignIntegrationTest, ProcessesCHeaderGlobPattern)
{
  // create multiple headers with structs that benefit from optimization
  utils::createTestFile(
      tempDir, "header1.h", "struct Test1 { char a; long b; int c; };");
  utils::createTestFile(
      tempDir, "header2.h", "struct Test2 { char x; long y; int z; };");
  utils::createTestFile(tempDir, "readme.txt", "this is not a header file");

  const std::string Header1 = (tempDir / "header1.h").string();
  const std::string Header2 = (tempDir / "header2.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {Header1, Header2});

  std::string globPattern = (tempDir / "*.h").string();
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {globPattern},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid()) << "alchemy::testing::integration::app should "
                                    "handle c header glob patterns";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::glob pattern "
                            "processing should succeed";

  // verify both .h files were transmuted (readme.txt should be ignored)
  const std::ifstream Header1File(tempDir / "header1.h");
  ASSERT_TRUE(Header1File.good());
  std::stringstream header1Content;
  header1Content << Header1File.rdbuf();

  const std::ifstream Header2File(tempDir / "header2.h");
  ASSERT_TRUE(Header2File.good());
  std::stringstream header2Content;
  header2Content << Header2File.rdbuf();

  // both structs should be optimized (long, int, char)
  ASSERT_EQ(header1Content.str(), "struct Test1 { long b; int c; char a; };");
  ASSERT_EQ(header2Content.str(), "struct Test2 { long y; int z; char x; };");

  // verify readme.txt was not modified
  const std::ifstream ReadmeFile(tempDir / "readme.txt");
  ASSERT_TRUE(ReadmeFile.good());
  std::stringstream readmeContent;
  readmeContent << ReadmeFile.rdbuf();
  ASSERT_EQ(readmeContent.str(), "this is not a header file");
}

TEST_F(SalignIntegrationTest, ProcessesCXXHeaderGlobPattern)
{
  // create multiple headers with structs that benefit from optimization
  utils::createTestFile(
      tempDir, "header1.hpp", "struct Test1 { char a; long b; int c; };");
  utils::createTestFile(
      tempDir, "header2.hpp", "struct Test2 { char x; long y; int z; };");
  utils::createTestFile(tempDir, "readme.txt", "this is not a header file");

  const std::string Header1 = (tempDir / "header1.hpp").string();
  const std::string Header2 = (tempDir / "header2.hpp").string();
  utils::createSyntheticCompilationDatabase(tempDir, {Header1, Header2});

  std::string globPattern = (tempDir / "*.hpp").string();
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {globPattern},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid()) << "alchemy::testing::integration::app should "
                                    "handle cxx header glob patterns";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::glob pattern "
                            "processing should succeed";

  // verify both .h files were transmuted (readme.txt should be ignored)
  const std::ifstream Header1File(tempDir / "header1.hpp");
  ASSERT_TRUE(Header1File.good());
  std::stringstream header1Content;
  header1Content << Header1File.rdbuf();

  const std::ifstream Header2File(tempDir / "header2.hpp");
  ASSERT_TRUE(Header2File.good());
  std::stringstream header2Content;
  header2Content << Header2File.rdbuf();

  // both structs should be optimized (long, int, char)
  ASSERT_EQ(header1Content.str(), "struct Test1 { long b; int c; char a; };");
  ASSERT_EQ(header2Content.str(), "struct Test2 { long y; int z; char x; };");

  // verify readme.txt was not modified
  const std::ifstream ReadmeFile(tempDir / "readme.txt");
  ASSERT_TRUE(ReadmeFile.good());
  std::stringstream readmeContent;
  readmeContent << ReadmeFile.rdbuf();
  ASSERT_EQ(readmeContent.str(), "this is not a header file");
}

// test with file that has no structs
TEST_F(SalignIntegrationTest, HandlesFileWithNoStructs)
{
  const std::string TestCode = R"(
#include <stdio.h>

int main() {
  printf("hello world\n");
  return 0;
}

void someFunction() {
  // no structs here
}
)";

  utils::createTestFile(tempDir, "no_structs.h", TestCode);
  std::string headerPath = (tempDir / "no_structs.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid()) << "alchemy::testing::integration::app should "
                                    "handle files with no structs";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::files with no "
                            "structs should complete successfully";

  // verify file remains unchanged (no transmutation needed)
  const std::ifstream ResultFile(headerPath);
  std::stringstream resultContent;
  resultContent << ResultFile.rdbuf();
  ASSERT_EQ(resultContent.str(), TestCode)
      << "alchemy::testing::integration::file with no structs should remain "
         "unchanged";
}

// test error handling for nonexistent file
TEST_F(SalignIntegrationTest, HandlesMissingFile)
{
  utils::createSyntheticCompilationDatabase(tempDir);
  std::string nonexistentPath = (tempDir / "does_not_exist.h").string();
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {nonexistentPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed even with "
         "missing files";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::missing files "
                            "should be handled gracefully by discovery";
}

// test salign parsing requirements: should only parse structs, not functions
TEST_F(SalignIntegrationTest, TestSAlignParsingRequirements)
{
  const std::string TestCode = R"(
struct TestStruct {
  char field1;
  double field2;
  int field3;
};

// this function should not be parsed since salign doesn't need functions
void someFunction() {
    return;
}
)";

  utils::createTestFile(tempDir, "mixed_content.h", TestCode);
  std::string headerPath = (tempDir / "mixed_content.h").string();
  utils::createSyntheticCompilationDatabase(tempDir, {headerPath});
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = tempDir,
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid()) << "alchemy::testing::integration::"
                                    "requirement-driven parsing should work";

  const int ExitCode = appResult.value().exec();
  ASSERT_EQ(ExitCode, 0) << "alchemy::testing::integration::mixed content "
                            "should be handled correctly";

  // verify integration: requirement-driven parsing → pipeline → transmutation
  // check that file was modified (proves orchestrator→parser→pipeline→transmute
  // chain worked)
  auto transmutedFile = tempDir / "mixed_content.h";
  auto modifiedContent = utils::readFile(transmutedFile);

  ASSERT_NE(modifiedContent, TestCode)
      << "alchemy::testing::integration::file should be modified if "
         "parse→analyze→transmute worked";

  // verify struct was reordered for optimization (proves analyzer processed
  // parser output correctly) Original: char field1; double field2; int field3;
  // Optimized: double field2; (int field3 | char field1); ... (larger fields
  // first)
  auto doublePos = modifiedContent.find("double field2");
  auto charPos = modifiedContent.find("char field1");
  ASSERT_LT(doublePos, charPos) << "alchemy::testing::integration::fields "
                                   "should be reordered (double before char) - "
                                   "verifies parser→analyzer data flow";
}

// test salign with explicit build directory
TEST_F(SalignIntegrationTest, UsesExplicitBuildDirectory)
{
  const std::string TestCode = R"(
struct UnoptimizedStruct {
  char a;
  double b;
  int c;
};
)";

  // create separate source and build directories
  auto srcDir = tempDir / "src";
  auto buildDir = tempDir / "build";
  std::filesystem::create_directories(srcDir);
  std::filesystem::create_directories(buildDir);

  // create source file in src directory
  auto sourceFile = utils::createTestFile(srcDir, "module.h", TestCode);
  std::string headerPath = sourceFile.string();

  // create compilation database in build directory
  utils::createSyntheticCompilationDatabase(buildDir, {headerPath});

  // run salign with explicit build directory
  auto appResult = utils::createAlchemyWithMockOptions(
      utils::MockCliConfig{.buildDir = buildDir,  // explicit build dir
                           .outputDir = tempDir,
                           .sourceFiles = {headerPath},
                           .excludePatterns = {},
                           .enableSalign = true});

  ASSERT_TRUE(appResult.valid())
      << "alchemy::testing::integration::app creation should succeed with "
         "explicit build dir";

  const int ExitCode = appResult.value().exec();

  ASSERT_EQ(ExitCode, 0)
      << "alchemy::testing::integration::salign should succeed when build dir "
         "contains compilation database";

  // verify struct was optimized
  const std::string ActualContent = utils::readFile(sourceFile);
  ASSERT_NE(ActualContent, TestCode)
      << "alchemy::testing::integration::struct should be optimized when using "
         "explicit build dir";
}

}  // namespace alchemy::testing
