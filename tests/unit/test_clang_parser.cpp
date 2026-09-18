// tests/unit/test_clang_parser.cpp
// unit tests for ClangParser

// std
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "parsing/libclang/clang_parser.hpp"
#include "parsing/parser.hpp"
#include "parsing/parsing_requirements.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class ClangParserTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory with valid C file
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_clang_parser_unit_test");

    testSourceFile = utils::createTestFile(
        tempDir, "test.c", "struct TestStruct { int x; };");

    // create compilation database for valid test
    utils::createSyntheticCompilationDatabase(tempDir,
                                              {testSourceFile.string()});
  }

  void
  TearDown() override
  {
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
  std::filesystem::path testSourceFile;
};

// test ClangParser::create() returns correct Result type on success
TEST_F(ClangParserTest, CreateReturnsValidResultType)
{
  const std::vector<std::filesystem::path> Sources = {testSourceFile};

  auto result = alchemy::parser::ClangParser::create(Sources, tempDir, {});

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::create should return "
                                 "valid Result with proper setup";
}

// test ClangParser::create() fails with empty source list
TEST_F(ClangParserTest, CreateFailsWithEmptySources)
{
  const std::vector<std::filesystem::path> Sources = {};

  auto result = alchemy::parser::ClangParser::create(Sources, {}, {});

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::create should fail with empty source list";
  ASSERT_FALSE(result.error().empty())
      << "alchemy::testing::unit::error message should not be empty";
}

// test created parser implements ParsingRuleAdapter interface
TEST_F(ClangParserTest, CreatedParserImplementsInterface)
{
  const std::vector<std::filesystem::path> Sources = {testSourceFile};
  auto result = alchemy::parser::ClangParser::create(Sources, tempDir, {});
  ASSERT_TRUE(result.valid());

  auto parser = std::move(result).value();
  alchemy::parser::ParsingRuleAdapter* adapter = parser.get();

  ASSERT_NE(adapter, nullptr) << "alchemy::testing::unit::parser should be "
                                 "castable to ParsingRuleAdapter";
  ASSERT_EQ(adapter->getName(), "Clang")
      << "alchemy::testing::unit::polymorphic getName() should work";
}

// test parse() respects needsStructParsing flag
TEST_F(ClangParserTest, ParseRespectsStructParsingFlag)
{
  const std::vector<std::filesystem::path> Sources = {testSourceFile};
  auto createResult =
      alchemy::parser::ClangParser::create(Sources, tempDir, {});
  ASSERT_TRUE(createResult.valid());

  auto parser = std::move(createResult.value());

  // test with parsing disabled
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = false;

  auto parseResult = parser->parse(requirements);

  ASSERT_TRUE(parseResult.valid());
  ASSERT_TRUE(parseResult.value().structs.empty())
      << "alchemy::testing::unit::parse with needsStructParsing=false should "
         "return empty structs";
}

// test ClangParser::create() handles malformed compilation database
TEST_F(ClangParserTest, CreateHandlesMalformedCompilationDatabase)
{
  // create directory with malformed compile_commands.json
  auto malformedDir = alchemy::testing::utils::createTempTestDirectory(
      "alchemy_malformed_test");

  // create source file
  auto sourceFile =
      utils::createTestFile(malformedDir, "test.c", "struct Test { int x; };");

  // create invalid JSON compilation database
  auto compileCommandsPath = malformedDir / "compile_commands.json";
  std::ofstream badJson(compileCommandsPath);
  badJson << "{ this is not valid json at all }\n";
  badJson.close();

  const std::vector<std::filesystem::path> Sources = {sourceFile};
  auto result = alchemy::parser::ClangParser::create(Sources, malformedDir, {});

  // With factory pattern, malformed database should now fail
  // (no more forgiving CommonOptionsParser fallback)
  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::create should fail with "
         "malformed database";

  // cleanup
  std::filesystem::remove_all(malformedDir);
}

// test ClangParser::create() handles missing compilation database
TEST_F(ClangParserTest, CreateHandlesMissingCompilationDatabase)
{
  // create directory without compile_commands.json
  auto noDbDir =
      alchemy::testing::utils::createTempTestDirectory("alchemy_no_db_test");

  // create source file but no compilation database
  auto sourceFile =
      utils::createTestFile(noDbDir, "test.c", "struct Test { int x; };");

  const std::vector<std::filesystem::path> Sources = {sourceFile};
  auto result = alchemy::parser::ClangParser::create(Sources, noDbDir, {});

  // With factory pattern, missing database should now fail
  // (compilation database is required)
  ASSERT_TRUE(result.invalid()) << "alchemy::testing::unit::create should fail "
                                   "without database";

  // cleanup
  std::filesystem::remove_all(noDbDir);
}

// test ClangParser::parse() can be called multiple times
TEST_F(ClangParserTest, ParseCanBeCalledMultipleTimes)
{
  const std::vector<std::filesystem::path> Sources = {testSourceFile};
  auto createResult =
      alchemy::parser::ClangParser::create(Sources, tempDir, {});
  ASSERT_TRUE(createResult.valid());

  auto parser = std::move(createResult).value();

  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;

  // first parse
  auto firstResult = parser->parse(requirements);
  ASSERT_TRUE(firstResult.valid())
      << "alchemy::testing::unit::first parse should succeed";
  ASSERT_FALSE(firstResult.value().structs.empty())
      << "alchemy::testing::unit::first parse should find structs";

  // second parse - verify ClangTool supports multiple runs
  auto secondResult = parser->parse(requirements);
  ASSERT_TRUE(secondResult.valid())
      << "alchemy::testing::unit::second parse should succeed (ClangTool "
         "supports multiple runs)";
  ASSERT_FALSE(secondResult.value().structs.empty())
      << "alchemy::testing::unit::second parse should also find structs";

  // verify both parses found the same struct
  ASSERT_EQ(firstResult.value().structs.size(),
            secondResult.value().structs.size())
      << "alchemy::testing::unit::multiple parses should return consistent "
         "results";
}

// test ClangParser::create() with valid build directory
TEST_F(ClangParserTest, CreateWithValidBuildDir)
{
  // create a separate build directory
  auto buildDir = tempDir / "build";
  std::filesystem::create_directories(buildDir);

  // create compilation database in build directory
  utils::createSyntheticCompilationDatabase(buildDir,
                                            {testSourceFile.string()});

  const std::vector<std::filesystem::path> Sources = {testSourceFile};
  auto result = alchemy::parser::ClangParser::create(Sources, buildDir, {});

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::create should succeed with valid build dir";
  ASSERT_NE(result.value(), nullptr)
      << "alchemy::testing::unit::should return valid parser";
}

// test ClangParser::create() with build directory containing no compilation
// database
TEST_F(ClangParserTest, CreateWithoutCompilationDatabase)
{
  // create build directory but don't put compile_commands.json in it
  auto buildDir = tempDir / "empty_build";
  std::filesystem::create_directories(buildDir);

  const std::vector<std::filesystem::path> Sources = {testSourceFile};
  auto result = alchemy::parser::ClangParser::create(Sources, buildDir, {});

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::create should not succeed when build dir "
         "does not have a compilation database";
}

}  // namespace alchemy::testing
