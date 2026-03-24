// src/parsing/libclang/compiler_adapters/clang_compilation_database_factory.cpp
// std
#include <filesystem>
#include <memory>
#include <string>
// src/parsing/libclang/compiler_adapters/clang_compilation_database_factory.cpp
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <fmt/format.h>

#include "fmt/core.h"

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_adapter.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/msvc_database_translator.hpp"

alchemy::core::Result<
    alchemy::parser::libclang::adapters::CompilationDatabaseInfo>
alchemy::parser::libclang::adapters::CompilationDatabaseFactory::fromBuildDir(
    const std::filesystem::path& buildDir)
{
  // load existing db using an inferred db wrapper for
  // source files that do not have corresponding compile commands
  // in the database (e.g. header only files)
  std::string dbError;
  auto dbPath = buildDir / "compile_commands.json";
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      dbPath.string(),
      dbError,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);
  if (!dbError.empty() and db == nullptr)
  {
    return alchemy::core::Result<
        alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
        failure(alchemy::core::Error::format(
            "alchemy::parser::libclang::adapters::CompilationDatabaseFactory",
            "compilation database not found\n"
            "  expected: {}\n"
            "  hint: configure your project first to generate a "
            "compile_commands.json\n"
            "  note: without a compilation database, alchemy cannot "
            "resolve symbols and will produce incorrect results",
            (buildDir / "compile_commands.json").string()));
  }

  // detect compiler and translate flags if needed
  std::vector<clang::tooling::CompileCommand> translatedCommands;
  std::string compilerType;

  if (alchemy::parser::libclang::adapters::IARDbTranslator::isIARCompiler(*db))
  {
    fmt::print("alchemy::{}parser{}::libclang::{}IAR compiler detected{}, "
               "translating flags...\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset);

    compilerType = "IAR";
    // extract query configuration from database
    auto queryConfigResult = IARDbTranslator::extractQueryConfig(*db);
    if (queryConfigResult.invalid())
    {
      return alchemy::core::Result<
          alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
          failure(alchemy::core::Error::format(
              "CompilationDatabaseFactory",
              "failed to extract IAR configuration: {}",
              queryConfigResult.error()));
    }

    // query compiler for system includes (validates compiler availability)
    auto includesResult =
        IARDbTranslator::querySystemIncludes(queryConfigResult.value());
    if (includesResult.invalid())
    {
      return alchemy::core::Result<
          alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
          failure(alchemy::core::Error::format(
              "CompilationDatabaseFactory",
              "IAR compiler query failed: {}\n"
              "  \n"
              "  Alchemy detected an IAR compilation database\n"
              "  but cannot access the IAR compiler to query system includes.\n"
              "  \n"
              "  Solutions:\n"
              "    1. Install IAR EWARM and add to PATH\n"
              "    2. Regenerate compile_commands.json\n"
              "  \n"
              "  Database: {}\n"
              "  Compiler: {}\n"
              "  Architecture: {}",
              includesResult.error(),
              dbPath.string(),
              queryConfigResult.value().compilerPath,
              fmt::join(queryConfigResult.value().archFlags, " ")));
    }

    auto definesResult =
        IARDbTranslator::querySystemDefines(queryConfigResult.value());
    if (definesResult.invalid())
    {
      return alchemy::core::Result<
          alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
          failure(alchemy::core::Error::format(
              "CompilationDatabaseFactory",
              "IAR compiler query failed: {}\n"
              "  \n"
              "  Alchemy detected an IAR compilation database\n"
              "  but cannot access the IAR compiler to query system defines.\n"
              "  \n"
              "  Solutions:\n"
              "    1. Install IAR EWARM and add to PATH\n"
              "    2. Regenerate compile_commands.json\n"
              "  \n"
              "  Database: {}\n"
              "  Compiler: {}\n"
              "  Architecture: {}",
              definesResult.error(),
              dbPath.string(),
              queryConfigResult.value().compilerPath,
              fmt::join(queryConfigResult.value().archFlags, " ")));
    }

    fmt::print(
        "alchemy::{}queried {} compiler{}: found {} system include paths\n",
        color::ansi::BrightGreen,
        queryConfigResult.value().compilerPath,
        color::ansi::Reset,
        includesResult.value().size());

    // create translator with query configuration
    const IARDbTranslator Translator(includesResult.value(),
                                     definesResult.value());
    translatedCommands = Translator.translateAll(*db);
  }
  else if (alchemy::parser::libclang::adapters::MSVCDbTranslator::
               isMSVCCompiler(*db))
  {
    fmt::print("alchemy::parser::libclang::{}MSVC compiler detected{}, "
               "translating flags...\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset);

    compilerType = "MSVC";
    const MSVCDbTranslator Translator;
    translatedCommands = Translator.translateAll(*db);
  }
  else
  {
    fmt::print(
        "alchemy::{}parser{}::libclang::{}GCC/Clang compiler detected{}, "
        "using existing database flags...\n",
        alchemy::color::ansi::BrightGreen,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::BrightGreen,
        alchemy::color::ansi::Reset);
    compilerType = "GCC/Clang";
    translatedCommands = db->getAllCompileCommands();
  }

  // create database from translated commands
  auto translatedDb = std::make_unique<
      alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter>(
      std::move(translatedCommands));

  // wrap with inference (applies to all compilers) for header only files or
  // files not in database
  auto inferredDb =
      clang::tooling::inferMissingCompileCommands(std::move(translatedDb));

  return alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
      success(alchemy::parser::libclang::adapters::CompilationDatabaseInfo{
          .database = std::move(inferredDb), .compilerType = compilerType});
}
