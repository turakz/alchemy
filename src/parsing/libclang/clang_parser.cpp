// src/clang_parser.cpp
// std
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/Tooling.h>
#include <fmt/core.h>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_parser.hpp"
#include "parsing/libclang/clang_struct_parsing_rule.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/parsing_requirements.hpp"

alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>
alchemy::parser::ClangParser::create(
    const std::vector<std::filesystem::path>& sourceFiles,
    const std::filesystem::path& buildDir)
{
  // no work found
  if (sourceFiles.empty())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "no source files to transmute\n"
                "  hint: double check your include or excludes"));
  }
  std::vector<std::string> sourceStrings;
  std::ranges::transform(sourceFiles,
                         std::back_inserter(sourceStrings),
                         [](const auto& file) { return file.string(); });

  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);
  if (dbResult.invalid())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "failed to load compilation database\n"
                "expected: {}\n"
                "error: {}\n"
                "  note: without a compilation database, alchemy cannot "
                "resolve symbols and will produce incorrect results",
                (buildDir / "compile_commands.json").string(),
                dbResult.error()));
  }

  // create tool from database (already has inference wrapper for headers)
  auto dbInfo = std::move(dbResult).value();
  auto tool = std::make_unique<clang::tooling::ClangTool>(*dbInfo.database,
                                                          sourceStrings);

  auto parser = std::make_unique<alchemy::parser::ClangParser>(
      std::move(tool), std::move(dbInfo.database), dbInfo.compilerType);

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
              "  note: parsing cannot continue with fatal errors - "
              "type information would be incorrect",
              clangResult));
    }

    // check for accumulated parsing errors from our custom matchers
    if (structParser->hasParseErrors())
    {
      const auto& errors = structParser->getParseErrors();
      std::string errorMsg = std::accumulate(
          std::next(errors.begin()),
          errors.end(),
          std::string("parsing failed with errors: ") + errors.front(),
          [](const std::string& acc, const std::string& err) {
            return acc + "; " + err;
          });
      return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
          failure(alchemy::core::Error::format(
              "alchemy::parser::ClangParser", "{}", errorMsg));
    }

    // extract struct results
    results.structs = structParser->getParsedStructs();
  }

  // future: parse functions if requirements.needsFunctionParsing
  // extract function results

  return alchemy::core::Result<
      alchemy::parser::artifacts::ParseResults>::success(std::move(results));
}
