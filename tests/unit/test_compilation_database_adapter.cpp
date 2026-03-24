// tests/unit/test_compilation_database_adapter.cpp
// unit tests for ClangCompilationDatabaseAdapter

// std
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

// 3rd party
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <fmt/core.h>
#include <gtest/gtest.h>

#include <gmock/gmock.h>

// local
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"
#include "parsing/libclang/compiler_adapters/gcc_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/msvc_database_translator.hpp"
#include "utils.hpp"

namespace alchemy::testing {

class CompilationDatabaseFactoryTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_compilation_database_factory_test");
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
    const std::filesystem::path mockCompilerBinaryPath{MOCK_CL_PATH};
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
    const std::filesystem::path mockCompilerBinaryPath{MOCK_ICCARM_PATH};
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
       TestIarTranslatorExtractQueryFailsWithEmptyDatabase)
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
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          *emptyDb);

  ASSERT_TRUE(queryResult.invalid());
  ASSERT_FALSE(queryResult.valid());
  ASSERT_THAT(queryResult.error(), ::testing::HasSubstr("no commands"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestInvalidIarTranslatorQueryIncludesFailsWithInvalidCompiler)
{
  // create config with non-existent compiler
  alchemy::parser::libclang::adapters::IarQueryConfig invalidQuery;
  invalidQuery.compilerPath = "/does/not/exist/fake/iccarm";
  invalidQuery.archFlags = {"--cpu=Cortex-M4"};

  // iccarm query should fail
  auto iarIncludesResult =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemIncludes(
          invalidQuery);

  ASSERT_FALSE(iarIncludesResult.valid());
  ASSERT_TRUE(iarIncludesResult.invalid());
  ASSERT_THAT(iarIncludesResult.error(),
              ::testing::HasSubstr("failed to execute"));
}

// Test 3: Compiler returns non-zero exit (using /usr/bin/false)
TEST_F(CompilationDatabaseFactoryTest, TestQueryIncludesFailsWithNonZeroExit)
{
  alchemy::parser::libclang::adapters::IarQueryConfig query;
  query.compilerPath = "/usr/bin/false";  // Always returns exit code 1
  query.archFlags = {};

  auto result =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemIncludes(
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
  ASSERT_THAT(dbResult.error(), ::testing::HasSubstr("could not query IAR"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsIarDatabase)
{
  setupIarDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "IAR");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsMsvcDatabase)
{
  setupMsvcDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "MSVC");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsGCCDatabase)
{
  setupGccDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "GCC");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestCompilationDatabaseFactoryLoadsClangDatabase)
{
  setupClangDatabase();
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);

  ASSERT_TRUE(dbResult.valid());
  ASSERT_NE(dbResult.value().database, nullptr);
  ASSERT_EQ(dbResult.value().compilerType, "Clang");
}

TEST_F(CompilationDatabaseFactoryTest, TestIarDbAdapterDetectsIarDatabase)
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
      alchemy::parser::libclang::adapters::IarDbTranslator::isIarCompiler(
          *iarDb));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::MsvcDbTranslator::isMsvcCompiler(
          *iarDb));
}

TEST_F(CompilationDatabaseFactoryTest, TestMsvcDbAdapterDetectsMsvcDatabase)
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
      alchemy::parser::libclang::adapters::MsvcDbTranslator::isMsvcCompiler(
          *msvcDb));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::IarDbTranslator::isIarCompiler(
          *msvcDb));
}

// Architecture-specific define tests
TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorCortexM0Defines)
{
  const std::filesystem::path mockCompilerPath{MOCK_ICCARM_PATH};

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
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv6-M defines are present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM6M__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM6M__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorCortexM3Defines)
{
  const std::filesystem::path mockCompilerPath{MOCK_ICCARM_PATH};

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
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv7-M defines are present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM7M__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM7M__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorCortexM33Defines)
{
  const std::filesystem::path mockCompilerPath{MOCK_ICCARM_PATH};

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
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify ARMv8-M Mainline defines are present
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__ARM8M_MAINLINE__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM8M_MAINLINE__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorFallbackDefines)
{
  const std::filesystem::path mockCompilerPath{MOCK_ICCARM_PATH};

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
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          *db);
  ASSERT_TRUE(queryConfig.valid());

  auto defines =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
          queryConfig.value());
  ASSERT_TRUE(defines.valid());

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      {}, defines.value());
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // Verify fallback to M4 defines (same as ARMv7E-M)
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM7EM__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM7EM__"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorAllowlistKeepsDIUFlags)
{
  // -D, -I, -U flags should pass through the allowlist
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm -DDEBUG -DSTM32F407xx -I/project/inc -I/project/drivers -UUNUSED_MACRO -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DSTM32F407xx"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-I/project/inc"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-I/project/drivers"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-UUNUSED_MACRO"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorAllowlistDropsIarFlags)
{
  // IAR-specific flags should be dropped by the allowlist
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm --cpu=Cortex-M4 --fpu=VFPv4_sp --endian=little --thumb --dlib_config normal --diag_suppress=Pa050,Pe223 --no_size_constraints --relaxed_fp --debug -DDEBUG -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // IAR flags should all be dropped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--cpu=Cortex-M4")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--fpu=VFPv4_sp")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--endian=little")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--thumb")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--dlib_config")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("normal")));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Not(::testing::Contains("--diag_suppress=Pa050,Pe223")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--no_size_constraints")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--relaxed_fp")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--debug")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-c")));

  // -D flag should still be present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorAllowlistDropsUnknownFlags)
{
  // unknown IAR flags (not in knownCompilerFlags) should also be dropped
  // by the allowlist — this is the improvement over the old blocklist
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm --some_future_iar_flag --another_new_flag=value -DDEBUG -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--some_future_iar_flag")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--another_new_flag=value")));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("main.c"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorSkipsOutputFlagAndArgument)
{
  // -o and its following argument should both be dropped
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm -DDEBUG -o build/main.o -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-o")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("build/main.o")));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("main.c"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorPreservesSourceFilePaths)
{
  // both relative and absolute source file paths should pass through
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/src/main.c",
    "command": "iccarm --cpu=Cortex-M4 -DDEBUG /tmp/test/src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("/tmp/test/src/main.c"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorAddsCompatibilityDefines)
{
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // IAR keywords defined away
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__intrinsic="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__noreturn="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__root="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ramfunc="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__no_init="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__task="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__irq="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__fiq="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__stackless="));

  // IAR attributes mapped to clang equivalents
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__packed=__attribute__((packed))"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__weak=__attribute__((weak))"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__inline=inline"));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Contains("-D__always_inline=__attribute__((always_inline))"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__noinline=__attribute__((noinline))"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorAddsTargetAndParsingFlags)
{
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test",
    "file": "/tmp/test/main.c",
    "command": "iccarm -c main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::IarDbTranslator Translator({}, {});
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // compiler replaced
  ASSERT_EQ(translated.CommandLine[0], "clang");

  // target and parsing flags appended
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-target"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("arm-none-eabi"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wno-everything"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fsyntax-only"));
}

TEST_F(CompilationDatabaseFactoryTest, TestIarTranslatorMixedRealisticCommand)
{
  // realistic IAR command with all categories: IAR flags (dropped),
  // -D/-I (kept), -o + arg (skipped), source file (kept)
  const std::string DbContent = R"(
[
  {
    "directory": "/home/embedded/project",
    "file": "/home/embedded/project/src/sensor.c",
    "command": "iccarm --cpu=Cortex-M4 --fpu=VFPv4_sp --diag_suppress=Pa050,Pe223 -I/home/embedded/project/include -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx --debug --endian=little --dlib_config normal -o build/sensor.o src/sensor.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const std::vector<std::string> SysIncludes = {"/opt/iar/arm/inc/c",
                                                "/opt/iar/arm/inc"};
  const std::vector<std::string> SysDefines = {"-D__ARM7EM__=1",
                                               "-D__CORE__=__ARM7EM__"};
  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      SysIncludes, SysDefines);
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // compiler replaced
  ASSERT_EQ(translated.CommandLine[0], "clang");

  // system includes injected
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("/opt/iar/arm/inc/c"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("/opt/iar/arm/inc"));

  // system defines injected
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__ARM7EM__=1"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__CORE__=__ARM7EM__"));

  // user -D/-I flags preserved
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-I/home/embedded/project/include"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DUSE_HAL_DRIVER"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DSTM32F407xx"));

  // source file preserved
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("src/sensor.c"));

  // IAR flags dropped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--cpu=Cortex-M4")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--fpu=VFPv4_sp")));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Not(::testing::Contains("--diag_suppress=Pa050,Pe223")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--debug")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--endian=little")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--dlib_config")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("normal")));

  // -o and its argument dropped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-o")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("build/sensor.o")));

  // compatibility defines present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__intrinsic="));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-D__packed=__attribute__((packed))"));

  // target and parsing flags
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("arm-none-eabi"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fsyntax-only"));

  // ARM EABI short enums injected
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fshort-enums"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccDbAdapterDetectsGccDatabase)
{
  setupGccDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::IarDbTranslator::isIarCompiler(*db));
  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::MsvcDbTranslator::isMsvcCompiler(
          *db));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccDbAdapterDoesNotDetectClangDatabase)
{
  setupClangDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorStripsGCCOnlyFlags)
{
  // create a database with GCC-only flags that should be stripped
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -DDEBUG -Wall -Werror -fcallgraph-info -Wformat-signedness -Wno-stringop-overread -fstack-usage -fvar-tracking-assignments -Wlogical-op -Wduplicated-branches -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // GCC-only flags should be stripped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fcallgraph-info")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wformat-signedness")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-stringop-overread")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fstack-usage")));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Not(::testing::Contains("-fvar-tracking-assignments")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wlogical-op")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wduplicated-branches")));
  // -Werror stripped: alchemy only needs AST, not warning enforcement
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Werror")));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorStripsGCCPrefixFlags)
{
  // test prefix-based stripping (flags with =value suffixes)
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -fcallgraph-info=su,da -fdump-tree-all -fipa-pta -Wsuggest-attribute=pure -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // prefix-matched GCC flags should be stripped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fcallgraph-info=su,da")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fdump-tree-all")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fipa-pta")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wsuggest-attribute=pure")));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorPreservesCommonFlags)
{
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -I/usr/local/include -DDEBUG -DVERSION=2 -std=c11 -Wall -Wextra -Werror -O2 -g -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // common flags shared by GCC and clang should be preserved
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-I/usr/local/include"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DVERSION=2"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-std=c11"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wall"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wextra"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Werror")));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-O2"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-g"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("src/main.c"));

  // compiler binary should be replaced with clang
  ASSERT_EQ(translated.CommandLine[0], "clang");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorAddsWnoUnknownWarningOption)
{
  setupGccDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // safety net flag should be added
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-unknown-warning-option"));
  // alchemy only needs AST, not codegen
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fsyntax-only"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGCCDetectionWithCrossCompilePrefix)
{
  // cross-compile GCC binaries like arm-none-eabi-gcc should be detected
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -mcpu=cortex-m4 -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorTranslatesGccFlagsToClangEquivalents)
{
  // every flag in the translation map should be converted, not stripped
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Wmaybe-uninitialized -Wno-maybe-uninitialized -Wvolatile -Wno-volatile -Wdiscarded-qualifiers -Wno-discarded-qualifiers -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // original GCC flags should not appear
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wmaybe-uninitialized")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-maybe-uninitialized")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wvolatile")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-volatile")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wdiscarded-qualifiers")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-discarded-qualifiers")));

  // clang equivalents should appear
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wconditional-uninitialized"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-conditional-uninitialized"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wdeprecated-volatile"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-deprecated-volatile"));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Contains("-Wincompatible-pointer-types-discards-qualifiers"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains(
                  "-Wno-incompatible-pointer-types-discards-qualifiers"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorMixedStripTranslatePassthrough)
{
  // realistic command: flags that should be stripped, translated, and passed
  // through all in one command line
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -DDEBUG -std=c17 -Wall -Wextra -Werror -O2 -g -fstack-usage -Wno-maybe-uninitialized -Wformat-signedness -Wno-volatile -fdump-tree-all -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // compiler replaced
  ASSERT_EQ(translated.CommandLine[0], "clang");

  // passthrough flags preserved
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-std=c17"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wall"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wextra"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Werror")));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-O2"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-g"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("src/main.c"));

  // stripped flags absent
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fstack-usage")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wformat-signedness")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fdump-tree-all")));

  // translated flags: originals absent, clang equivalents present
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-maybe-uninitialized")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-conditional-uninitialized"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Wno-volatile")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-deprecated-volatile"));

  // safety net and syntax-only appended
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-unknown-warning-option"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fsyntax-only"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGCCDetectionViaFlagFallback)
{
  // unknown compiler name, but GCC-only flags in arguments trigger detection
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "/opt/custom/compiler -Iinclude -fstack-usage -Wall -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest, TestGCCDetectionViaFlagPrefixFallback)
{
  // unknown compiler name, GCC-only prefix flag triggers detection
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "/opt/custom/compiler -Iinclude -fdump-tree-all -Wall -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_TRUE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorStripsWerrorPrefixVariants)
{
  // -Werror=<warning> flags should be stripped as prefix matches
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -Wall -Werror=format -Werror=suggest-attribute=pure -o main.o -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-Werror=format")));
  ASSERT_THAT(
      translated.CommandLine,
      ::testing::Not(::testing::Contains("-Werror=suggest-attribute=pure")));
  // common flags preserved
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Wall"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorAddsClangCompatibilityFlags)
{
  // translator should add suppression flags for common GCC→clang friction
  setupGccDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // GCC→clang compatibility flags for embedded code
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-ignored-attributes"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("-Wno-typedef-redefinition"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccParseSystemIncludesFromOutput)
{
  // sample output from `gcc -E -Wp,-v -x c /dev/null 2>&1`
  const std::string GccOutput =
      "ignoring nonexistent directory \"/usr/local/include\"\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/lib/gcc/arm-none-eabi/10.3.1/include\n"
      " /usr/lib/gcc/arm-none-eabi/10.3.1/include-fixed\n"
      " /usr/lib/gcc/arm-none-eabi/10.3.1/../../../arm-none-eabi/include\n"
      "End of search list.\n";

  auto includes =
      alchemy::parser::libclang::adapters::GccDbTranslator::parseSystemIncludes(
          GccOutput);

  ASSERT_EQ(includes.size(), 3);
  ASSERT_EQ(includes[0], "/usr/lib/gcc/arm-none-eabi/10.3.1/include");
  ASSERT_EQ(includes[1], "/usr/lib/gcc/arm-none-eabi/10.3.1/include-fixed");
  ASSERT_EQ(includes[2],
            "/usr/lib/gcc/arm-none-eabi/10.3.1/../../../arm-none-eabi/include");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccParseSystemIncludesStripsFrameworkSuffix)
{
  // macOS gcc output includes " (framework directory)" suffix
  const std::string GccOutput = "#include <...> search starts here:\n"
                                " /usr/local/include\n"
                                " /Library/Frameworks (framework directory)\n"
                                "End of search list.\n";

  auto includes =
      alchemy::parser::libclang::adapters::GccDbTranslator::parseSystemIncludes(
          GccOutput);

  ASSERT_EQ(includes.size(), 2);
  ASSERT_EQ(includes[0], "/usr/local/include");
  ASSERT_EQ(includes[1], "/Library/Frameworks");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccParseSystemIncludesEmptyOnNoSearchList)
{
  auto includes =
      alchemy::parser::libclang::adapters::GccDbTranslator::parseSystemIncludes(
          "some random output\nno search list here\n");

  ASSERT_TRUE(includes.empty());
}

TEST_F(CompilationDatabaseFactoryTest, TestGccExtractQueryConfigFromDatabase)
{
  // GCC database with arm cross-compiler
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto configResult =
      alchemy::parser::libclang::adapters::GccDbTranslator::extractQueryConfig(
          *db);

  ASSERT_TRUE(configResult.valid());
  ASSERT_EQ(configResult.value().compilerPath, "arm-none-eabi-gcc");
}

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorInjectsSystemIncludes)
{
  // create a database and translator with known system includes
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  std::vector<std::string> sysIncludes = {"/usr/lib/gcc/include",
                                          "/usr/arm-none-eabi/include"};

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      sysIncludes);
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // system includes injected as -isystem
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-isystem"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("/usr/lib/gcc/include"));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("/usr/arm-none-eabi/include"));

  // original flags still present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccQueryTargetTripleFromHostCompiler)
{
  // query the host compiler's target triple via -dumpmachine
  alchemy::parser::libclang::adapters::GccQueryConfig query;
  query.compilerPath = "cc";

  auto result =
      alchemy::parser::libclang::adapters::GccDbTranslator::queryTargetTriple(
          query);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::host compiler should return a target triple";
  ASSERT_FALSE(result.value().empty());
  // host triple should contain a hyphen (e.g., x86_64-linux-gnu)
  ASSERT_NE(result.value().find('-'), std::string::npos);
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccQueryTargetTripleFailsWithInvalidCompiler)
{
  alchemy::parser::libclang::adapters::GccQueryConfig query;
  query.compilerPath = "/does/not/exist/fake-gcc";

  auto result =
      alchemy::parser::libclang::adapters::GccDbTranslator::queryTargetTriple(
          query);

  ASSERT_FALSE(result.valid());
  ASSERT_THAT(result.error(), ::testing::HasSubstr("failed to query"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorInjectsTargetTripleWhenProvided)
{
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-none-eabi");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-target"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("arm-none-eabi"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorDoesNotInjectTargetTripleWhenEmpty)
{
  setupGccDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  // default constructor — no target triple
  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-target")));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorPassesThroughInferredTargetFlag)
{
  // when inferTargetAndDriverMode adds --target=arm-none-eabi to a command,
  // the GCC translator should preserve it in the translated output
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc --target=arm-none-eabi -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-none-eabi");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // --target= from inferTargetAndDriverMode should pass through translation
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("--target=arm-none-eabi"));
}

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorStripsDriverModeFlag)
{
  // inferTargetAndDriverMode may inject --driver-mode=gcc or --driver-mode=g++
  // the GCC translator should strip these since it handles flag translation
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc --target=arm-none-eabi --driver-mode=gcc -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-none-eabi");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // --driver-mode= should be stripped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("--driver-mode=gcc")));
  // --target= should be preserved
  ASSERT_THAT(translated.CommandLine,
              ::testing::Contains("--target=arm-none-eabi"));
  // user flags should still be present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
}

// --- validateTargetTriple tests ---

TEST_F(CompilationDatabaseFactoryTest, TestValidateTargetTripleAcceptsArmTriple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("arm-none-eabi");
  ASSERT_EQ(result, "arm-none-eabi");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsArmHfTriple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("arm-none-eabihf");
  ASSERT_EQ(result, "arm-none-eabihf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsAarch64Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("aarch64-none-elf");
  ASSERT_EQ(result, "aarch64-none-elf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsX86_64Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("x86_64-linux-gnu");
  ASSERT_EQ(result, "x86_64-linux-gnu");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsI686Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("i686-linux-gnu");
  ASSERT_EQ(result, "i686-linux-gnu");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsRiscv32Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("riscv32-unknown-elf");
  ASSERT_EQ(result, "riscv32-unknown-elf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsRiscv64Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("riscv64-unknown-linux-gnu");
  ASSERT_EQ(result, "riscv64-unknown-linux-gnu");
}

TEST_F(CompilationDatabaseFactoryTest, TestValidateTargetTripleAcceptsAvrTriple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("avr-unknown-unknown");
  ASSERT_EQ(result, "avr-unknown-unknown");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsMsp430Triple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("msp430-none-elf");
  ASSERT_EQ(result, "msp430-none-elf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsThumbVariantTriple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("thumbv7em-none-eabihf");
  ASSERT_EQ(result, "thumbv7em-none-eabihf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleAcceptsArmv7emVariantTriple)
{
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("armv7em-none-eabihf");
  ASSERT_EQ(result, "armv7em-none-eabihf");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleWarnsOnUnrecognizedArchitecture)
{
  // should return the triple unchanged but print a warning to stderr
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("obscure-vendor-os-abi");
  ASSERT_EQ(result, "obscure-vendor-os-abi");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTripleHandlesNoDashTriple)
{
  // degenerate case: no dash in triple (architecture only)
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("arm");
  ASSERT_EQ(result, "arm");
}

TEST_F(CompilationDatabaseFactoryTest,
       TestValidateTargetTriplePassesThroughUnrecognizedTriple)
{
  // unrecognized triple should be returned as-is (not modified)
  auto result = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple("mycustomcpu-vendor-os");
  ASSERT_EQ(result, "mycustomcpu-vendor-os");
}

// --- nostdinc/nobuiltininc stripping tests ---

TEST_F(CompilationDatabaseFactoryTest, TestGccTranslatorStripsNostdincFlags)
{
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -nostdinc -nobuiltininc -nostdlibinc -nostdinc++ -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator;
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // all flags that suppress clang's resource directory should be stripped
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-nostdinc")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-nobuiltininc")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-nostdlibinc")));
  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-nostdinc++")));

  // user flags should still be present
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-Iinclude"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-DDEBUG"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorPreservesArchitectureFlags)
{
  // architecture flags must pass through to clang for correct type layout
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -march=armv7e-m -Iinclude -DDEBUG -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-none-eabi");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  // architecture flags must be preserved for correct type layout
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-mcpu=cortex-m4"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-mthumb"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-mfloat-abi=hard"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-mfpu=fpv4-sp-d16"));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-march=armv7e-m"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorInjectsShortEnumsForArmEabi)
{
  // ARM EABI targets default to short enums (-fshort-enums) in GCC.
  // clang does not mirror this default, so the translator must inject it.
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-none-eabi-gcc -mcpu=cortex-m4 -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-none-eabi");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-fshort-enums"));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorDoesNotInjectShortEnumsForX86)
{
  // x86 targets do not use short enums by default
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "gcc -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "x86_64-linux-gnu");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fshort-enums")));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccTranslatorDoesNotInjectShortEnumsForArmLinux)
{
  // ARM Linux targets do not use short enums by default
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "arm-linux-gnueabihf-gcc -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      {}, "arm-linux-gnueabihf");
  auto commands = db->getAllCompileCommands();
  auto translated = Translator.translateCommand(commands[0]);

  ASSERT_THAT(translated.CommandLine,
              ::testing::Not(::testing::Contains("-fshort-enums")));
}

// --- isGccCompiler edge case tests ---

TEST_F(CompilationDatabaseFactoryTest,
       TestGccDetectionReturnsFalseForEmptyDatabase)
{
  setupEmptyDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccDetectionReturnsFalseForUnknownCompilerWithoutGccFlags)
{
  // unknown compiler binary without any GCC-only flags should not be detected
  const std::string DbContent = R"(
[
  {
    "directory": "/tmp/test_project",
    "file": "/tmp/test_project/src/main.c",
    "command": "/opt/custom/some-compiler -Iinclude -DDEBUG -Wall -O2 -c src/main.c"
  }
])";
  writeFile(DbContent, buildDir / "compile_commands.json");

  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  ASSERT_FALSE(
      alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(*db));
}

TEST_F(CompilationDatabaseFactoryTest,
       TestGccExtractQueryConfigFailsWithEmptyDatabase)
{
  setupEmptyDatabase();
  std::string dbError;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      (buildDir / "compile_commands.json").string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);

  ASSERT_TRUE(dbError.empty());
  ASSERT_NE(db, nullptr);

  auto configResult =
      alchemy::parser::libclang::adapters::GccDbTranslator::extractQueryConfig(
          *db);

  ASSERT_TRUE(configResult.invalid());
  ASSERT_THAT(configResult.error(), ::testing::HasSubstr("no commands"));
}

// ============================================================================
// compiler_utils edge cases
// ============================================================================

TEST_F(CompilationDatabaseFactoryTest, InferMissingIncludeFlags_AllPresent)
{
  // when all includes are already present in args, result should be empty
  std::vector<std::string> args = {"clang", "-I/inc/a", "-I/inc/b", "file.c"};
  std::vector<std::string> allIncludes = {"/inc/a", "/inc/b"};

  auto result = alchemy::parser::libclang::adapters::inferMissingIncludeFlags(
      args, allIncludes);

  ASSERT_TRUE(result.empty())
      << "no missing includes should produce empty result";
}

TEST_F(CompilationDatabaseFactoryTest, FindArgInsertionPoint_NoSeparator)
{
  // without "--", insertion point should be before the last element
  std::vector<std::string> args = {"clang", "-Ifoo", "file.c"};

  auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(args);

  ASSERT_NE(it, args.end());
  ASSERT_EQ(*it, "file.c")
      << "insertion point should be before the last element (source file)";
}

TEST_F(CompilationDatabaseFactoryTest, FindArgInsertionPoint_WithSeparator)
{
  // with "--", insertion point should be at the "--" separator
  std::vector<std::string> args = {"clang", "-Ifoo", "--", "file.c"};

  auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(args);

  ASSERT_NE(it, args.end());
  ASSERT_EQ(*it, "--") << "insertion point should be at the -- separator";
}

// ============================================================================
// extractBuildTarget tests
// ============================================================================

TEST_F(CompilationDatabaseFactoryTest,
       ExtractBuildTarget_CmakeOutputReturnsTargetName)
{
  // standard CMake output path: .../CMakeFiles/<target>.dir/...
  const std::string output =
      "App/io4/MK24/CMakeFiles/App.MK24.dir/__/Source/AppLed.c.o";

  auto result = alchemy::parser::libclang::adapters::extractBuildTarget(output);

  ASSERT_EQ(result, "App.MK24");
}

TEST_F(CompilationDatabaseFactoryTest,
       ExtractBuildTarget_EmptyOutputReturnsDefault)
{
  auto result = alchemy::parser::libclang::adapters::extractBuildTarget("");

  ASSERT_EQ(result, "default");
}

TEST_F(CompilationDatabaseFactoryTest,
       ExtractBuildTarget_NonCmakePathReturnsDefault)
{
  // path with no CMakeFiles pattern
  const std::string output = "/tmp/build/src/main.o";

  auto result = alchemy::parser::libclang::adapters::extractBuildTarget(output);

  ASSERT_EQ(result, "default");
}

TEST_F(CompilationDatabaseFactoryTest,
       ExtractBuildTarget_CmakeFilesWithoutDirSuffixReturnsDefault)
{
  // CMakeFiles present but no .dir suffix after target name
  const std::string output = "CMakeFiles/App.MK24/something.o";

  auto result = alchemy::parser::libclang::adapters::extractBuildTarget(output);

  ASSERT_EQ(result, "default");
}

TEST_F(CompilationDatabaseFactoryTest,
       ExtractBuildTarget_CmakeOutputNestedPathReturnsTargetName)
{
  // deeper nested path, still a valid CMake output
  const std::string output = "/home/user/project/Build/tests/"
                             "CMakeFiles/MyLib.Cortex-M4.dir/src/module.c.o";

  auto result = alchemy::parser::libclang::adapters::extractBuildTarget(output);

  ASSERT_EQ(result, "MyLib.Cortex-M4");
}

// ============================================================================
// groupCommandsByTarget tests
// ============================================================================

TEST_F(CompilationDatabaseFactoryTest,
       GroupCommandsByTarget_TwoDistinctTargetsProduceTwoGroups)
{
  clang::tooling::CompileCommand cmd1;
  cmd1.Filename = "/src/a.c";
  cmd1.Output = "Build/CMakeFiles/App.MK24.dir/__/Source/a.c.o";

  clang::tooling::CompileCommand cmd2;
  cmd2.Filename = "/src/b.c";
  cmd2.Output = "Build/CMakeFiles/App.PSOC6.dir/__/Source/b.c.o";

  auto groups =
      alchemy::parser::libclang::adapters::groupCommandsByTarget({cmd1, cmd2});

  ASSERT_EQ(groups.size(), 2u);
  ASSERT_EQ(groups.count("App.MK24"), 1u);
  ASSERT_EQ(groups.count("App.PSOC6"), 1u);
  ASSERT_EQ(groups.at("App.MK24").size(), 1u);
  ASSERT_EQ(groups.at("App.PSOC6").size(), 1u);
}

TEST_F(CompilationDatabaseFactoryTest,
       GroupCommandsByTarget_NoOutputFieldAllUnderDefault)
{
  clang::tooling::CompileCommand cmd1;
  cmd1.Filename = "/src/a.c";
  // Output is empty by default

  clang::tooling::CompileCommand cmd2;
  cmd2.Filename = "/src/b.c";
  // Output is empty by default

  auto groups =
      alchemy::parser::libclang::adapters::groupCommandsByTarget({cmd1, cmd2});

  ASSERT_EQ(groups.size(), 1u);
  ASSERT_EQ(groups.count("default"), 1u);
  ASSERT_EQ(groups.at("default").size(), 2u);
}

TEST_F(CompilationDatabaseFactoryTest,
       GroupCommandsByTarget_MultipleCommandsSameTargetGroupedTogether)
{
  clang::tooling::CompileCommand cmd1;
  cmd1.Filename = "/src/a.c";
  cmd1.Output = "Build/CMakeFiles/App.MK24.dir/__/Source/a.c.o";

  clang::tooling::CompileCommand cmd2;
  cmd2.Filename = "/src/b.c";
  cmd2.Output = "Build/CMakeFiles/App.MK24.dir/__/Source/b.c.o";

  clang::tooling::CompileCommand cmd3;
  cmd3.Filename = "/src/c.c";
  cmd3.Output = "Build/CMakeFiles/App.PSOC6.dir/__/Source/c.c.o";

  auto groups = alchemy::parser::libclang::adapters::groupCommandsByTarget(
      {cmd1, cmd2, cmd3});

  ASSERT_EQ(groups.size(), 2u);
  ASSERT_EQ(groups.at("App.MK24").size(), 2u);
  ASSERT_EQ(groups.at("App.PSOC6").size(), 1u);
}

TEST_F(CompilationDatabaseFactoryTest,
       GroupCommandsByTarget_EmptyInputProducesEmptyMap)
{
  auto groups = alchemy::parser::libclang::adapters::groupCommandsByTarget({});

  ASSERT_TRUE(groups.empty());
}

}  // namespace alchemy::testing
