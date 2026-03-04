// tests/integration/parsing/test_realistic_databases.cpp
// Integration tests for parser with realistic compilation databases

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
    tempDir = std::filesystem::temp_directory_path() / "alchemy_parser_db_test";
    std::filesystem::create_directories(tempDir);

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

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithGCCDatabase)
{
  // create realistic GCC database (only has source file entries, NO headers)
  alchemy::testing::utils::createGCCDatabase(buildDir, projectDir);

  // parse header files that are NOT in the database
  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"  // header-only, no .c file
  };

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with headers not in database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "GCC/Clang")
      << "Should detect GCC/Clang compiler type";

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
  alchemy::testing::utils::createGCCDatabase(buildDir, projectDir);

  // mix of source files (in database) and headers (not in database)
  const std::vector<std::filesystem::path> MixedFiles = {
      projectDir / "main.c",                  // in database
      projectDir / "inc" / "config.h",        // not in database
      projectDir / "src" / "utils.c",         // in database
      projectDir / "inc" / "header_only.h"};  // not in database, no .c file

  auto parserResult =
      alchemy::parser::ClangParser::create(MixedFiles, buildDir);

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

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithClangDatabase)
{
  alchemy::testing::utils::createClangDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should successfully create with Clang database: "
      << parserResult.error();

  // verify correct compiler type detected
  ASSERT_EQ(parserResult.value()->getName(), "GCC/Clang")
      << "Should detect GCC/Clang compiler type";

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
      alchemy::parser::ClangParser::create(MixedFiles, buildDir);

  ASSERT_TRUE(parserResult.valid())
      << "Parser should handle mixed files with GCC/Clang database: "
      << parserResult.error();

  ASSERT_EQ(parserResult.value()->getName(), "GCC/Clang")
      << "Should detect GCC/Clang compiler type";

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

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithIARDatabase)
{
  alchemy::testing::utils::createIARDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir);

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

TEST_F(ParserDatabaseIntegrationTest, ParsesHeadersWithMSVCDatabase)
{
  alchemy::testing::utils::createMSVCDatabase(buildDir, projectDir);

  const std::vector<std::filesystem::path> HeaderFiles = {
      projectDir / "inc" / "config.h",
      projectDir / "inc" / "utils.h",
      projectDir / "inc" / "header_only.h"};

  auto parserResult =
      alchemy::parser::ClangParser::create(HeaderFiles, buildDir);

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
