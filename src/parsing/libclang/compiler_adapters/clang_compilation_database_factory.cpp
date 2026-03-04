// src/parsing/libclang/compiler_adapters/clang_compilation_database_factory.cpp
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"

// std
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <fmt/core.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_adapter.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"
#include "parsing/libclang/compiler_adapters/gcc_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"
#include "parsing/libclang/compiler_adapters/msvc_database_translator.hpp"

namespace alchemy::parser::libclang::adapters::detail {

struct TranslatedCommands {
  std::string compilerType;
  std::vector<clang::tooling::CompileCommand> commands;
};

alchemy::core::Result<TranslatedCommands>
buildIarTranslatedCommands(const clang::tooling::CompilationDatabase& targetDb,
                           const std::filesystem::path& dbPath)
{
  fmt::print("alchemy::{}parser{}::libclang::{}IAR compiler detected{}, "
             "translating flags...\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);

  // extract query configuration from database
  auto queryConfigResult =
      alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
          targetDb);
  if (queryConfigResult.invalid())
  {
    return alchemy::core::Result<TranslatedCommands>::failure(
        alchemy::core::Error::format(
            "alchemy::parser::CompilationDatabaseFactory",
            "failed to extract IAR configuration: {}",
            queryConfigResult.error()));
  }

  // query compiler for system includes (validates compiler availability)
  auto includesResult =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemIncludes(
          queryConfigResult.value());
  if (includesResult.invalid())
  {
    return alchemy::core::Result<TranslatedCommands>::failure(
        alchemy::core::Error::format(
            "alchemy::parser::CompilationDatabaseFactory",
            "could not query IAR system includes: {}\n"
            "  database: {}\n"
            "  compiler: {}",
            includesResult.error(),
            dbPath.string(),
            queryConfigResult.value().compilerPath));
  }

  auto definesResult =
      alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
          queryConfigResult.value());
  if (definesResult.invalid())
  {
    return alchemy::core::Result<TranslatedCommands>::failure(
        alchemy::core::Error::format(
            "alchemy::parser::CompilationDatabaseFactory",
            "could not query IAR system defines: {}\n"
            "  database: {}\n"
            "  compiler: {}",
            definesResult.error(),
            dbPath.string(),
            queryConfigResult.value().compilerPath));
  }

  fmt::print("alchemy::{}queried{} {}{}{} compiler: found {}{}{} system "
             "include paths\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             queryConfigResult.value().compilerPath,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             includesResult.value().size(),
             alchemy::color::ansi::Reset);

  // create translator with query configuration
  const alchemy::parser::libclang::adapters::IarDbTranslator Translator(
      includesResult.value(), definesResult.value());

  return alchemy::core::Result<TranslatedCommands>::success(TranslatedCommands{
      "IAR",
      alchemy::parser::libclang::adapters::translateDb(Translator, targetDb)});
}

alchemy::core::Result<TranslatedCommands>
buildMsvcTranslatedCommands(const clang::tooling::CompilationDatabase& targetDb)
{
  fmt::print("alchemy::{}parser{}::libclang::{}MSVC compiler detected{}, "
             "translating flags...\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);

  const alchemy::parser::libclang::adapters::MsvcDbTranslator Translator;

  return alchemy::core::Result<TranslatedCommands>::success(TranslatedCommands{
      "MSVC",
      alchemy::parser::libclang::adapters::translateDb(Translator, targetDb)});
}

alchemy::core::Result<TranslatedCommands>
buildGccTranslatedCommands(const clang::tooling::CompilationDatabase& targetDb,
                           const std::filesystem::path& dbPath)
{
  fmt::print("alchemy::{}parser{}::libclang::{}GCC compiler detected{}, "
             "translating flags...\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);

  // extract query configuration from database
  auto queryConfigResult =
      alchemy::parser::libclang::adapters::GccDbTranslator::extractQueryConfig(
          targetDb);
  std::vector<std::string> sysIncludes;

  if (queryConfigResult.valid())
  {
    // query compiler for system includes (non-fatal on failure)
    auto includesResult = alchemy::parser::libclang::adapters::GccDbTranslator::
        querySystemIncludes(queryConfigResult.value());
    if (includesResult.valid())
    {
      sysIncludes = std::move(includesResult).value();
      fmt::print("alchemy::{}queried{} {}{}{} compiler: found {}{}{} system "
                 "include paths\n",
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BrightGreen,
                 queryConfigResult.value().compilerPath,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BrightGreen,
                 sysIncludes.size(),
                 alchemy::color::ansi::Reset);
    }
    else
    {
      fmt::print(stderr,
                 "alchemy::{}parser{}::{}warning{}: could not query GCC "
                 "system includes: {}\n"
                 "  database: {}\n"
                 "  compiler: {}\n",
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::Yellow,
                 alchemy::color::ansi::Reset,
                 includesResult.error(),
                 dbPath.string(),
                 queryConfigResult.value().compilerPath);
    }
  }

  // resolve target triple: first check if inferTargetAndDriverMode already
  // inferred it from the compiler binary name (works for clang-based names
  // like arm-none-eabi-clang), then fall back to querying the compiler via
  // -dumpmachine (required for GCC names like arm-none-eabi-gcc, since
  // LLVM's driver suffix table doesn't include gcc/g++)
  std::string targetTriple;
  {
    auto allCmds = targetDb.getAllCompileCommands();
    if (!allCmds.empty())
    {
      for (const auto& arg : allCmds[0].CommandLine)
      {
        if (arg.starts_with("--target="))
        {
          targetTriple = arg.substr(9);
          break;
        }
      }
    }
  }

  if (!targetTriple.empty())
  {
    fmt::print("alchemy::{}inferred{} target architecture from compiler "
               "name: {}{}{}\n",
               alchemy::color::ansi::BoldBrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::BrightGreen,
               targetTriple,
               alchemy::color::ansi::Reset);
  }
  else if (queryConfigResult.valid())
  {
    // fallback: query compiler for target triple via -dumpmachine
    auto tripleResult =
        alchemy::parser::libclang::adapters::GccDbTranslator::queryTargetTriple(
            queryConfigResult.value());
    if (tripleResult.valid())
    {
      targetTriple = std::move(tripleResult).value();
      fmt::print("alchemy::{}queried{} {}{}{} compiler: target "
                 "architecture {}{}{}\n",
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BrightGreen,
                 queryConfigResult.value().compilerPath,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::BrightGreen,
                 targetTriple,
                 alchemy::color::ansi::Reset);
    }
    else
    {
      fmt::print(stderr,
                 "alchemy::{}parser{}::{}warning{}: could not query GCC "
                 "target triple: {}\n"
                 "  database: {}\n"
                 "  compiler: {}\n",
                 alchemy::color::ansi::BoldBrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::Yellow,
                 alchemy::color::ansi::Reset,
                 tripleResult.error(),
                 dbPath.string(),
                 queryConfigResult.value().compilerPath);
    }
  }

  const alchemy::parser::libclang::adapters::GccDbTranslator Translator(
      sysIncludes, targetTriple);

  return alchemy::core::Result<TranslatedCommands>::success(TranslatedCommands{
      "GCC",
      alchemy::parser::libclang::adapters::translateDb(Translator, targetDb)});
}

TranslatedCommands
buildClangPassthroughCommands(
    const clang::tooling::CompilationDatabase& targetDb)
{
  fmt::print("alchemy::{}parser{}::libclang::{}Clang compiler detected{}, "
             "using existing database flags...\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             alchemy::color::ansi::Reset);

  return TranslatedCommands{"Clang", targetDb.getAllCompileCommands()};
}

}  // namespace alchemy::parser::libclang::adapters::detail

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
  if (!dbError.empty() && db == nullptr)
  {
    return alchemy::core::Result<
        alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
        failure(alchemy::core::Error::format(
            "alchemy::parser::CompilationDatabaseFactory",
            "compilation database not found\n"
            "  expected: {}\n"
            "  hint: configure your project first to generate a "
            "compile_commands.json\n"
            "  note: without a compilation database, alchemy cannot "
            "resolve symbols and will produce incorrect results",
            (buildDir / "compile_commands.json").string()));
  }

  // wrap with target/driver-mode inference: automatically adds --target= and
  // --driver-mode= flags based on the compiler binary name in CommandLine[0]
  // (e.g., arm-none-eabi-gcc → --target=arm-none-eabi --driver-mode=gcc)
  // -> this is only relevant to clang-compiled projects
  auto targetDb = clang::tooling::inferTargetAndDriverMode(std::move(db));

  // detect compiler and translate flags
  alchemy::core::Result<
      alchemy::parser::libclang::adapters::detail::TranslatedCommands>
      translatedResult = [&]() {
        if (alchemy::parser::libclang::adapters::IarDbTranslator::isIarCompiler(
                *targetDb))
        {
          return alchemy::parser::libclang::adapters::detail::
              buildIarTranslatedCommands(*targetDb, dbPath);
        }
        if (alchemy::parser::libclang::adapters::MsvcDbTranslator::
                isMsvcCompiler(*targetDb))
        {
          return alchemy::parser::libclang::adapters::detail::
              buildMsvcTranslatedCommands(*targetDb);
        }
        if (alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(
                *targetDb))
        {
          return alchemy::parser::libclang::adapters::detail::
              buildGccTranslatedCommands(*targetDb, dbPath);
        }
        return alchemy::core::Result<
            alchemy::parser::libclang::adapters::detail::TranslatedCommands>::
            success(alchemy::parser::libclang::adapters::detail::
                        buildClangPassthroughCommands(*targetDb));
      }();

  if (translatedResult.invalid())
  {
    return alchemy::core::Result<
        alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
        failure(std::move(translatedResult).error());
  }

  auto translated = std::move(translatedResult).value();

  auto allIncludePaths =
      alchemy::parser::libclang::adapters::extractIncludePaths(
          translated.commands);

  // create database from translated commands
  auto translatedDb = std::make_unique<
      alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter>(
      std::move(translated.commands));

  // wrap with inference (applies to all compilers) for header only files or
  // files not in database
  auto inferredDb =
      clang::tooling::inferMissingCompileCommands(std::move(translatedDb));

  return alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
      success(alchemy::parser::libclang::adapters::CompilationDatabaseInfo{
          .database = std::move(inferredDb),
          .compilerType = std::move(translated.compilerType),
          .allIncludePaths = std::move(allIncludePaths)});
}
