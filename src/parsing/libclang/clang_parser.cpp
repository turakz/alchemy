// src/clang_parser.cpp
#include "parsing/libclang/clang_parser.hpp"

// std
#include <cstddef>
#include <cstdio>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <fmt/core.h>
#include <llvm/ADT/StringRef.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_struct_parsing_rule.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"
#include "parsing/parsing_requirements.hpp"

alchemy::core::Result<
    alchemy::parser::libclang::adapters::CompilationDatabaseInfo>
alchemy::parser::ClangParser::loadCompilationDatabase(
    const std::filesystem::path& buildDir)
{
  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);
  if (dbResult.invalid())
  {
    return alchemy::core::Result<
        alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
        failure(alchemy::core::Error::format(
            "alchemy::parser::ClangParser",
            "failed to load compilation database\n"
            "expected: {}\n"
            "error: {}\n"
            "  note: without a compilation database, alchemy cannot "
            "resolve symbols and will produce incorrect results",
            (buildDir / "compile_commands.json").string(),
            dbResult.error()));
  }
  return alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
      success(std::move(dbResult).value());
}

std::unordered_map<std::string, std::vector<std::string>>
alchemy::parser::ClangParser::buildTranslationUnitIndex(
    const std::vector<std::string>& dbFiles)
{
  std::unordered_map<std::string, std::vector<std::string>> stemToDbFiles;
  for (const auto& dbFile : dbFiles)
  {
    auto stem = std::filesystem::path(dbFile).stem().string();
    stemToDbFiles[stem].push_back(dbFile);
  }
  return stemToDbFiles;
}

alchemy::parser::ParseTargetResult
alchemy::parser::ClangParser::resolveParseTargets(
    const std::vector<std::filesystem::path>& sourceFiles,
    const std::unordered_map<std::string, std::vector<std::string>>& tuIndex)
{
  // header→TU pairing with collision handling:
  // - unique stem match: pair with the TU (full compilation context)
  // - multiple stem matches: parse header directly via
  // inferMissingCompileCommands
  //   (the database is already wrapped with inferMissingCompileCommands which
  //   uses directory-proximity heuristics to synthesize the best compile
  //   command)
  // - no stem match: parse header directly (header-only)
  //
  // this avoids the ambiguity of first-match-wins when multiple TUs share
  // the same basename (e.g. io4/MK24/Foo.c vs io4/PSOC6/Foo.c) while
  // preserving full TU compilation context for unambiguous cases.
  alchemy::parser::ParseTargetResult result;

  static const std::unordered_set<std::string> HeaderExtensions = {
      ".h", ".hh", ".hpp", ".hxx"};

  // deduplicate TUs that get paired from multiple headers
  std::unordered_set<std::string> pairedTUs;

  for (const auto& file : sourceFiles)
  {
    auto absPath = std::filesystem::absolute(file).lexically_normal().string();
    result.targetHeaders.insert(absPath);

    auto ext = file.extension().string();
    if (!HeaderExtensions.contains(ext))
    {
      // source file (.c/.cpp) — parse directly
      result.filesToParse.push_back(absPath);
      ++result.sourceCount;
      continue;
    }

    // header file — try stem-based TU pairing
    auto stem = file.stem().string();
    auto itr = tuIndex.find(stem);

    if (itr != tuIndex.end() && itr->second.size() == 1)
    {
      // unique match — pair with the TU for full compilation context
      if (pairedTUs.insert(itr->second[0]).second)
      {
        result.filesToParse.push_back(itr->second[0]);
      }
      ++result.pairedCount;
    }
    else
    {
      // multiple matches (collision) or no match — parse header directly
      // via inferMissingCompileCommands which uses directory-proximity
      // heuristics to find the best-matching TU's flags
      result.headerFiles.insert(absPath);
      result.filesToParse.push_back(absPath);
      ++result.directCount;
    }
  }

  fmt::print("alchemy::{}parser{}::paired {}{}{} headers with TUs, "
             "{}{}{} direct (inferred), {}{}{} source files\n",
             alchemy::color::ansi::BoldBrightGreen,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             result.pairedCount,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             result.directCount,
             alchemy::color::ansi::Reset,
             alchemy::color::ansi::BrightGreen,
             result.sourceCount,
             alchemy::color::ansi::Reset);

  return result;
}

std::vector<std::string>
alchemy::parser::ClangParser::resolveStdPreamble(
    const std::unordered_set<std::string>& headerFiles,
    const std::vector<std::string>& includePaths)
{
  // resolve std-type preamble for directly-parsed headers. these headers
  // are parsed via inferMissingCompileCommands and may use std types
  // (size_t, uint8_t, bool) without #including the std headers.
  std::vector<std::string> preamble;
  if (headerFiles.empty())
  {
    return preamble;
  }

  static constexpr std::array<const char*, 3> StdHeaders = {
      "stddef.h", "stdint.h", "stdbool.h"};
  for (const auto* header : StdHeaders)
  {
    for (const auto& dir : includePaths)
    {
      auto path = std::filesystem::path(dir) / header;
      if (std::filesystem::exists(path))
      {
        preamble.emplace_back("-include");
        preamble.emplace_back(path.string());
        break;
      }
    }
  }

  return preamble;
}

void
alchemy::parser::ClangParser::injectIncludePaths(
    clang::tooling::ClangTool& tool,
    const std::vector<std::string>& dbFiles,
    std::vector<std::string> allIncludes)
{
  // augment inferred commands with project-wide include paths
  // -> headers not in the database get commands via LLVM's interpolation,
  // but the proxy file's -I flags may not cover all includes the header
  // needs -> inject the union of all -I paths for inferred files
  if (allIncludes.empty())
  {
    return;
  }

  std::unordered_set<std::string> knownFiles(std::begin(dbFiles),
                                             std::end(dbFiles));

  tool.appendArgumentsAdjuster(
      [knownFiles = std::move(knownFiles),
       allIncludes = std::move(allIncludes)](
          const clang::tooling::CommandLineArguments& args,
          llvm::StringRef filename) -> clang::tooling::CommandLineArguments {
        if (knownFiles.contains(filename.str()))
        {
          return args;
        }

        auto missingFlags =
            alchemy::parser::libclang::adapters::inferMissingIncludeFlags(
                args, allIncludes);

        if (missingFlags.empty())
        {
          return args;
        }

        auto augmented = args;
        auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(
            augmented);
        for (auto& flag : missingFlags)
        {
          it = std::next(augmented.insert(it, std::move(flag)));
        }

        return augmented;
      });
}

void
alchemy::parser::ClangParser::injectStdPreamble(
    clang::tooling::ClangTool& tool,
    std::unordered_set<std::string> headerFiles,
    std::vector<std::string> preamble)
{
  // inject std-type preamble for header files:
  // headers parsed directly (via inferMissingCompileCommands) may use
  // std types (size_t, uint8_t, bool) without #including the std headers.
  if (preamble.empty())
  {
    return;
  }

  tool.appendArgumentsAdjuster(
      [headerFiles = std::move(headerFiles), preamble = std::move(preamble)](
          const clang::tooling::CommandLineArguments& args,
          llvm::StringRef filename) -> clang::tooling::CommandLineArguments {
        if (!headerFiles.contains(filename.str()))
        {
          return args;
        }

        auto augmented = args;
        auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(
            augmented);

        for (const auto& flag : preamble)
        {
          it = std::next(augmented.insert(it, flag));
        }

        return augmented;
      });
}

alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>
alchemy::parser::ClangParser::create(
    const std::vector<std::filesystem::path>& sourceFiles,
    const std::filesystem::path& buildDir)
{
  if (sourceFiles.empty())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "no source files to transmute\n"
                "  hint: double check your include or excludes"));
  }

  auto dbResult =
      alchemy::parser::ClangParser::loadCompilationDatabase(buildDir);
  if (dbResult.invalid())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            std::move(dbResult).error());
  }
  auto dbInfo = std::move(dbResult).value();

  auto dbFiles = dbInfo.database->getAllFiles();

  auto tuIndex =
      alchemy::parser::ClangParser::buildTranslationUnitIndex(dbFiles);

  auto targets =
      alchemy::parser::ClangParser::resolveParseTargets(sourceFiles, tuIndex);

  auto tool = std::make_unique<clang::tooling::ClangTool>(*dbInfo.database,
                                                          targets.filesToParse);

  auto preamble = alchemy::parser::ClangParser::resolveStdPreamble(
      targets.headerFiles, dbInfo.allIncludePaths);

  alchemy::parser::ClangParser::injectIncludePaths(
      *tool, dbFiles, std::move(dbInfo.allIncludePaths));

  alchemy::parser::ClangParser::injectStdPreamble(
      *tool, std::move(targets.headerFiles), std::move(preamble));

  auto parser = std::make_unique<alchemy::parser::ClangParser>(
      std::move(tool),
      std::move(dbInfo.database),
      dbInfo.compilerType,
      std::move(targets.targetHeaders));

  return alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>::
      success(std::move(parser));
}

alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
alchemy::parser::ClangParser::parse(const ParsingRequirements& requirements)
{
  alchemy::parser::artifacts::ParseResults results;

  // parse structs if requested
  if (requirements.needsStructParsing)
  {
    // set up parser and clang-tool front-end
    auto structParser = std::make_unique<ClangStructParsingRule>();
    clang::ast_matchers::MatchFinder finder;

    // register matchers and run clang-tool front-end against discovered files
    structParser->registerMatchers(finder);
    static_cast<void>(std::fflush(
        stdout));  // flush prior output before clang's progress prints
    int clangResult =
        m_tool->run(clang::tooling::newFrontendActionFactory(&finder).get());

    // check if clang reported fatal errors during parsing
    if (clangResult != 0)
    {
      return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
          failure(alchemy::core::Error::format(
              "alchemy::parser::ClangParser",
              "reported fatal errors during parsing (exit code: {})\n"
              "  hint: check that all includes can be resolved with the "
              "provided compilation database\n"
              "  note: parsing cannot continue with fatal errors, "
              "type information would be incorrect",
              clangResult));
    }

    // check for accumulated parsing errors from our custom matchers
    if (structParser->hasParseErrors())
    {
      const auto& errors = structParser->getParseErrors();
      std::string errorMsg = std::accumulate(
          std::next(std::begin(errors)),
          std::end(errors),
          std::string("parsing failed with errors: ") + errors.front(),
          [](const std::string& acc, const std::string& err) {
            return acc + "; " + err;
          });
      return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
          failure(alchemy::core::Error::format(
              "alchemy::parser::ClangParser", "{}", errorMsg));
    }

    // extract struct results and filter to target headers
    results.structs = structParser->getParsedStructs();

    // post-filter: keep only structs whose source file is in the target set
    if (!m_targetHeaders.empty())
    {
      std::erase_if(results.structs, [this](const auto& s) {
        return !m_targetHeaders.contains(s.sourceFile);
      });
    }

    // deduplicate: multiple TUs including the same header can produce
    // duplicate struct extractions — keep first occurrence per
    // (sourceFile, structName) pair
    {
      std::unordered_set<std::string> seen;
      std::erase_if(results.structs, [&seen](const auto& s) {
        auto key = s.sourceFile + "::" + s.structName;
        return !seen.insert(key).second;
      });
    }
  }

  return alchemy::core::Result<
      alchemy::parser::artifacts::ParseResults>::success(std::move(results));
}
