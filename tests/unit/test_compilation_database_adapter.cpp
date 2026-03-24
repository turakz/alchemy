// tests/unit/test_compilation_database_adapter.cpp
// unit tests for ClangCompilationDatabaseAdapter

// std
#include <chrono>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

// 3rd party
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <gtest/gtest.h>

#include "fmt/core.h"

#include <gmock/gmock.h>

// local
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/msvc_database_translator.hpp"

namespace alchemy::testing {

class CompilationDatabaseFactoryTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    tempDir = std::filesystem::temp_directory_path() /
              "alchemy_compilation_database_factory_test" /
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(tempDir);
    projectDir = tempDir / "project";
    buildDir = tempDir / "build";
    std::filesystem::create_directories(buildDir);
  }
  void
  TearDown() override
  {
  }

  void
  setupMsvcDatabase()
  {
    auto mockCompilerBinaryPath = std::filesystem::absolute(
        std::filesystem::current_path().parent_path() / "data" /
        "mock_compilers" / "mock_cl");
    const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "C:/Projects/test_project",
    "file": "C:/Projects/test_project/src/main.c",
    "command": "{0} /Iinclude /DDEBUG /TC /W4 /Zi /Od /Fo:main.obj /c src/main.c"
  }},
  {{
    "directory": "C:/Projects/test_project",
    "file": "C:/Projects/test_project/src/utils.c",
    "command": "{0} /Iinclude /DNDEBUG /TC /O2 /GL /Fo:utils.obj /c src/utils.c"
  }}
])",
                                              mockCompilerBinaryPath.string());
    writeFile(DbContent, buildDir / "compile_commands.json");
  }

  void
  setupIarDatabase()
  {
    auto mockCompilerBinaryPath = std::filesystem::absolute(
        std::filesystem::current_path().parent_path() / "data" /
        "mock_compilers" / "mock_iccarm");
    const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "/home/embedded/project",
    "file": "/home/embedded/project/src/sensor.c",
    "command": "{0} --cpu=Cortex-M4 --fpu=VFPv4_sp --diag_suppress=Pa050,Pe223 --include_path=$PROJ_DIR$/include --include_path=$TOOLKIT_DIR$/inc/c -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx --debug --endian=little --dlib_config normal -o sensor.o src/sensor.c"
  }},
  {{
    "directory": "/home/embedded/project",
    "file": "/home/embedded/project/src/main.c",
    "command": "{0} --cpu=Cortex-M4 --fpu=VFPv4_sp --diag_suppress=Pa050 --include_path=$PROJ_DIR$/include -DNDEBUG -DUSE_HAL_DRIVER --endian=little --thumb -o main.o src/main.c"
  }},
  {{
    "directory": "/home/embedded/project",
    "file": "/home/embedded/project/src/interrupts.c",
    "command": "{0} --cpu=Cortex-M7 --diag_warning=Pe188 --dlib_config full --include_path=$PROJ_DIR$/include -DDEBUG -o interrupts.o src/interrupts.c"
  }}
])",
                                              mockCompilerBinaryPath.string());
    writeFile(DbContent, buildDir / "compile_commands.json");
  }
  void
  setupInvalidIarDatabase()
  {
    const std::string DbContent = R"(
  [
    {
      "directory": "/tmp/test",
      "file": "/tmp/test/main.c",
      "command": "/nonexistent/iccarm --cpu=Cortex-M4 -c main.c"
    }
  ])";

    writeFile(DbContent, buildDir / "compile_commands.json");
  }

  void
  setupGccDatabase()
  {
    const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -DDEBUG -std=c11 -Wall -Wextra -o main.o -c src/main.c"
  },
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/utils.c",
    "command": "gcc -Iinclude -DNDEBUG -std=c11 -O2 -o utils.o -c src/utils.c"
  }
])";
    writeFile(DbContent, buildDir / "compile_commands.json");
  }

  void
  setupClangDatabase()
  {
    const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "clang -Iinclude -DDEBUG -std=c11 -Wall -Wextra -fsanitize=address -o main.o -c src/main.c"
  },
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/utils.c",
    "command": "clang -Iinclude -DNDEBUG -std=c11 -O3 -flto -o utils.o -c src/utils.c"
  }
])";
    writeFile(DbContent, buildDir / "compile_commands.json");
  }

  void
  setupEmptyDatabase()
  {
    const std::string EmptyDbContent = R"([])";
    writeFile(EmptyDbContent, buildDir / "compile_commands.json");
  }

  std::filesystem::path tempDir;
  std::filesystem::path projectDir;
  std::filesystem::path buildDir;

  void
  writeFile(const std::string_view Content, const std::filesystem::path& path)
  {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << Content;
    file.close();
  }
};

TEST_F(CompilationDatabaseFactoryTest,
       TestIARTranslatorExtractQueryFailsWithEmptyDatabase)
{
  setupEmptyDatabase();
  // load empty db
  std::string dbError;
  auto emptyDb = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(emptyDb, nullptr);

  // extract query config should fail due to empty db
  auto queryResult =
      alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
          *emptyDb);

  ASSERT_TRUE(queryResult.invalid());
  ASSERT_FALSE(queryResult.valid());
  ASSERT_THAT(queryResult.error(), ::testing::HasSubstr("no commands"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestInvalidIARTranslatorQueryIncludesFailsWithInvalidCompiler)
{
  // create config with non-existent compiler
  alchemy::parser::libclang::adapters::IARQueryConfig invalidQuery;
  invalidQuery.compilerPath = "/does/not/exist/fake/iccarm";
  invalidQuery.archFlags = {"--cpu=Cortex-M4"};

  // iccarm query should fail
  auto iarIncludesResult =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemIncludes(
          invalidQuery);

  ASSERT_FALSE(iarIncludesResult.valid());
  ASSERT_TRUE(iarIncludesResult.invalid());
  ASSERT_THAT(iarIncludesResult.error(),
              ::testing::HasSubstr("failed to execute"));
}

// Test 3: Compiler returns non-zero exit (using /usr/bin/false)
TEST_F(CompilationDatabaseFactoryTest, TestQueryIncludesFailsWithNonZeroExit)
{
  alchemy::parser::libclang::adapters::IARQueryConfig query;
  query.compilerPath = "/usr/bin/false";  // Always returns exit code 1
  query.archFlags = {};

  auto result =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemIncludes(
          query);

  ASSERT_FALSE(result.valid());
  ASSERT_THAT(result.error(), ::testing::HasSubstr("command failed"));
}

// Test 4: Factory fails gracefully when compiler doesn't exist
TEST_F(CompilationDatabaseFactoryTest,
       TestFactoryFailsGracefullyWithInvalidCompiler)
{
  setupInvalidIarDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_FALSE(dbResult.valid());
  ASSERT_THAT(dbResult.error(),
              ::testing::HasSubstr("IAR compiler query failed"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsIARDatabase)
{
  setupIarDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "IAR");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsMSVCDatabase)
{
  setupMsvcDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "MSVC");
}

TEST_F(CompilationDatabaseFactoryTest, TestLoadsGccClangDatabase)
{
  // gcc should work with default database
  setupGccDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "GCC/Clang");

  // clang and gcc are pretty much identical
  setupClangDatabase();
  dbResult = alchemy::parser::libclang::adapters::CompilationDatabaseFactory::
      fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "GCC/Clang");
}

TEST_F(CompilationDatabaseFactoryTest, TestIARDbAdapterDetectsIarDatabase)
{
  setupIarDatabase();
  std::string dbError;
  auto dbPath = buildDir / "compile_commands.json";
  auto iarDb = clang::tooling::JSONCompilationDatabase::loadFromFile(
      dbPath.string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(iarDb, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::IARDbTranslator::isIARCompiler(
          *iarDb));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::MSVCDbTranslator::isMSVCCompiler(
          *iarDb));
}

TEST_F(CompilationDatabaseFactoryTest, TestMSVCDbAdapterDetectsMSVCDatabase)
{
  setupMsvcDatabase();
  std::string dbError;
  auto dbPath = buildDir / "compile_commands.json";
  auto msvcDb = clang::tooling::JSONCompilationDatabase::loadFromFile(
      dbPath.string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(msvcDb, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::MSVCDbTranslator::isMSVCCompiler(
          *msvcDb));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::IARDbTranslator::isIARCompiler(
          *msvcDb));
}

// Architecture-specific define tests
TEST_F(CompilationDatabaseFactoryTest, TestIARTranslatorCortexM0Defines)
{
  auto mockCompilerPath =
      std::filesystem::absolute(std::filesystem::current_path().parent_path() /
                                "data" / "mock_compilers" / "mock_iccarm");

  const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "{0} --cpu=Cortex-M0 -c main.c"
  }}
])",
                                            mockCompilerPath.string());

  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto queryConfig =
      alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IARDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv6-M defines are present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM6M__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM6M__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIARTranslatorCortexM3Defines)
{
  auto mockCompilerPath =
      std::filesystem::absolute(std::filesystem::current_path().parent_path() /
                                "data" / "mock_compilers" / "mock_iccarm");

  const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "{0} --cpu=Cortex-M3 -c main.c"
  }}
])",
                                            mockCompilerPath.string());

  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto queryConfig =
      alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IARDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv7-M defines are present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM7M__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM7M__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIARTranslatorCortexM33Defines)
{
  auto mockCompilerPath =
      std::filesystem::absolute(std::filesystem::current_path().parent_path() /
                                "data" / "mock_compilers" / "mock_iccarm");

  const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "{0} --cpu=Cortex-M33 -c main.c"
  }}
])",
                                            mockCompilerPath.string());

  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto queryConfig =
      alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IARDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv8-M Mainline defines are present
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__ARM8M_MAINLINE__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM8M_MAINLINE__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIARTranslatorFallbackDefines)
{
  auto mockCompilerPath =
      std::filesystem::absolute(std::filesystem::current_path().parent_path() /
                                "data" / "mock_compilers" / "mock_iccarm");

  // No --cpu flag in command (should fallback to M4 defines)
  const std::string DbContent = fmt::format(R"(
[
  {{
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "{0} -c main.c"
  }}
])",
                                            mockCompilerPath.string());

  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto queryConfig =
      alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IARDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IARDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify fallback to M4 defines (same as ARMv7E-M)
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM7EM__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM7EM__"));
}

}  // namespace alchemy::testing
