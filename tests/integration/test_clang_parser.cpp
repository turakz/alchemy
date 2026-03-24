// tests/integration/test_clang_parser.cpp
// integration tests for parser with realistic compilation databases

// std
#include <filesystem>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "parsing/libclang/clang_parser.hpp"
#include "parsing/parsing_requirements.hpp"
#include "utils.hpp"

class ParserDatabaseIntegrationTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temp directory for this test
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_parser_db_test", false);

    // copy shared_project to temp directory
    auto sharedProjectSrc =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "data" /
        "shared_project";
    projectDir = tempDir / "project";
    alchemy::testing::utils::copyDirectory(sharedProjectSrc, projectDir);

    // create build directory
    buildDir = projectDir / "build";
    std::filesystem::create_directories(buildDir);
  }

  void
  TearDown() override
  {
    // clean up temp directory
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
  std::filesystem::path projectDir;
  std::filesystem::path buildDir;
};

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithGccDatabase)
{
  // create realistic GCC database (only has source file entries, NO headers)
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  // parse header files that are NOT in the database
  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"  // header-only, no .c file
  };

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with headers not in database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "GCC")
      << "Should detect GCC compiler type";

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse headers via inference: "
      << parseResult.error();

  // verify expected structs were found (Config, Util, HeaderOnlyStruct)
  ASSERT_EQ(parseResult.value().structs.size(), 3)
      << "Should find exactly 3 structs (Config, Util, HeaderOnlyStruct)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesMixedFilesWithGCCDatabase)
{
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  // mix of source files (in database) and headers (not in database)
  const std::vector<std::filesystem::path> MixedFiles = {
      projectDir / "main.c",                  // in database
      projectDir / "inc" / "config.h",        // not in database
      projectDir / "src" / "utils.c",         // in database
      projectDir / "inc" / "header_only.h"};  // not in database, no .c file

  auto parserResult =
      alchemy::parser::ClangParser::create(MixedFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with mixed files: "
      << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse mixed files: "
      << parseResult.error();

  // mixed files: main.c (0 structs), config.h (1), utils.c (0), header_only.h
  // (1) Note: utils.h is not directly passed but included by utils.c
  ASSERT_EQ(parseResult.value().structs.size(), 2)
      << "Should find exactly 2 structs (Config from config.h, "
         "HeaderOnlyStruct from header_only.h)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesHeaderUsingStdTypesViaTUPairing)
{
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {projectDir / "inc" /
                                                          "std_types.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should create with std_types.h: " << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Header using size_t/bool/uint8_t without #includes should parse "
         "via TU pairing: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 1)
      << "Should find StdTypesStruct";

  const auto& s = parseResult.value().structs[0];
  EXPECT_EQ(s.structName, "StdTypesStruct");
  EXPECT_EQ(s.fields.size(), 5)
      << "Should have 5 fields (size_t, uint8_t, bool, uint32_t, int16_t)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithClangDatabase)
{
  alchemy::testing::utils::createClangDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with Clang database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "Clang")
      << "Should detect Clang compiler type";

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse headers with Clang database: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 3)
      << "Should find exactly 3 structs (Config, Util, HeaderOnlyStruct)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesMixedFilesWithClangDatabase)
{
  alchemy::testing::utils::createClangDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> MixedFiles = {
      projectDir / "main.c",
      projectDir / "inc" / "config.h",
      projectDir / "src" / "utils.c",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(MixedFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should handle mixed files with Clang database: "
      << parserResult.error();

  ASSERT_EQ(parserResult.value()->getName(), "Clang")
      << "Should detect Clang compiler type";

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse mixed files with Clang database: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 2)
      << "Should find exactly 2 structs (Config from config.h, "
         "HeaderOnlyStruct from header_only.h)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithIarDatabase)
{
  alchemy::testing::utils::createIarDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with IAR database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "IAR")
      << "Should detect IAR compiler type";

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);
  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse headers with IAR database: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 3)
      << "Should find exactly 3 structs (Config, Util, HeaderOnlyStruct)";
}

// ============================================================================
// Exclude pattern filtering tests
// ============================================================================

TEST_F(ParserDatabaseIntegrationTest,
       ExcludePatternsNonMatchingPreservesAllCommands)
{
  // regression: non-matching exclude patterns must not suppress any TUs.
  // this exercises the filter lambda path (all commands pass through).
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  // pattern that matches nothing in this project
  const std::vector<std::string> ExcludePatterns = {"**/nonexistent_dir/**"};

  auto parserResult = alchemy::parser::ClangParser::create(
      HeaderFiles, buildDir, ExcludePatterns);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should create with non-matching exclude patterns: "
      << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parse should succeed with non-matching exclude patterns: "
      << parseResult.error();

  // non-matching patterns must not filter any TUs: same struct count as
  // the baseline GCC test with empty patterns
  ASSERT_EQ(parseResult.value().structs.size(), 3)
      << "Non-matching exclude patterns should not suppress any TUs — "
         "same 3 structs (Config, Util, HeaderOnlyStruct) must be found";
}

TEST_F(ParserDatabaseIntegrationTest,
       ExcludePatternsFilterMatchingCommandsParseSucceeds)
{
  // when exclude patterns match a subset of compile commands, those TUs are
  // removed from the dep graph. target headers that lose all dep graph entries
  // fall through to direct parse. parse must still succeed and structs are
  // still found (via direct fallback).
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  // headers exclusively included by src/*.c TUs
  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "std_types.h",    // only included by src/std_types.c
      projectDir / "inc" / "task_private.h"  // only included by src/task.c
  };

  // exclude all TUs under src/ —> std_types.c and task.c are filtered
  const std::vector<std::string> ExcludePatterns = {"**/src/**"};

  auto parserResult = alchemy::parser::ClangParser::create(
      HeaderFiles, buildDir, ExcludePatterns);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should create when some commands are excluded: "
      << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parse should succeed even when matching TUs are filtered — "
         "headers fall through to direct parse: "
      << parseResult.error();

  // structs still found via direct-parse fallback even though their dep-graph
  // TUs were excluded
  ASSERT_FALSE(parseResult.value().structs.empty())
      << "Structs should still be found via direct parse fallback";
}

TEST_F(ParserDatabaseIntegrationTest,
       ExcludePatternsFilterAllCommandsAllHeadersDirectParse)
{
  // edge case: when all compile commands are excluded, all target headers fall
  // through to direct parse via the fallback inference DB. the parser must not
  // crash or hard-fail —> zero dep-graph specs is a valid state.
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  // match every .c file — filters all 5 TUs in the GCC database
  const std::vector<std::string> ExcludePatterns = {"**/*.c"};

  auto parserResult = alchemy::parser::ClangParser::create(
      HeaderFiles, buildDir, ExcludePatterns);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should create even when all compile commands are excluded: "
      << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parse should succeed with all commands excluded -> no hard failure: "
      << parseResult.error();

  // with no compile commands in the fallback DB, inferMissingCompileCommands
  // has nothing to interpolate include paths from, so all direct-parse
  // attempts fail gracefully (soft-skipped). the result is an empty struct
  // list rather than a hard error —> the parser handles this case without
  // crashing.
  ASSERT_TRUE(parseResult.value().structs.empty())
      << "When all commands are excluded the fallback DB is empty, all "
         "direct-parse attempts soft-skip, and no structs are found "
         "but parse() itself must still return success (no hard failure)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesPrivateHeaderViaDirectParseFallback)
{
  // regression test: private headers with no matching TU in the dep graph
  // (clang -MM finds no TU that includes task_private.h in this fixture)
  // fall through to direct parse via inferMissingCompileCommands + global
  // include injection. the struct must still be found.
  alchemy::testing::utils::createGccDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {projectDir / "inc" /
                                                          "task_private.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});
  ASSERT_TRUE(parserResult.valid())
      << "Parser should create with task_private.h: " << parserResult.error();

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Private header with no dep graph match should parse "
         "via direct parse fallback: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 1)
      << "Should find TaskContext struct";

  const auto& s = parseResult.value().structs[0];
  EXPECT_EQ(s.structName, "TaskContext");
  EXPECT_EQ(s.fields.size(), 4)
      << "Should have 4 fields (taskId, priority, stackSize, flags)";
}

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithMsvcDatabase)
{
  alchemy::testing::utils::createMsvcDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir, {});

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with MSVC database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "MSVC")
      << "Should detect MSVC compiler type";

  const alchemy::parser::ParsingRequirements Requirements{.needsStructParsing =
                                                              true};
  auto parseResult = parserResult.value()->parse(Requirements);

  ASSERT_TRUE(parseResult.valid())
      << "Parser should successfully parse headers with MSVC database: "
      << parseResult.error();

  ASSERT_EQ(parseResult.value().structs.size(), 3)
      << "Should find exactly 3 structs (Config, Util, HeaderOnlyStruct)";
}
