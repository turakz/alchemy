// inc/clang_parser.hpp
#ifndef ALCHEMY_PARSING_CLANG_PARSER_HPP
#define ALCHEMY_PARSING_CLANG_PARSER_HPP

// std
#include <cstddef>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/parser.hpp"
#include "parsing/parsing_requirements.hpp"

namespace alchemy::parser {

// reverse dependency map entry: a TU that includes a given header.
struct TranslationUnit {
  std::string tuPath;
  clang::tooling::CompileCommand command;
};

// maps fully-resolved header absolute path → TUs that include it.
// built by buildReverseDependencyMap() via clang -MM.
using ReverseDependencyMap =
    std::unordered_map<std::string, std::vector<TranslationUnit>>;

// a resolved parse command: run ClangTool on parseFile using command,
// tag resulting structs with targetName.
// parseFile is a .c/.cpp TU (for headers resolved via dep graph),
// or a header itself (direct parse via inferMissingCompileCommands fallback).
struct ParseCommand {
  std::string parseFile;   // file actually passed to ClangTool
  std::string targetName;  // from extractBuildTarget(cmd.output)
  clang::tooling::CompileCommand command;  // exact translated compile command
};

// clang-specific parser adapter.
// resolves user source files to per-command ParseCommands globally (not per
// CMake target), then runs an isolated ClangTool per spec.
// use create() to construct.
class ClangParser : public ParsingRuleAdapter {
public:
  // factory method: resolves sourceFiles against the compilation database in
  // buildDir and prepares per-command parse specs.
  [[nodiscard]] static alchemy::core::Result<
      std::unique_ptr<alchemy::parser::ClangParser>>
  create(const std::vector<std::filesystem::path>& sourceFiles,
         const std::filesystem::path& buildDir,
         const std::vector<std::string>& excludePatterns);

  [[nodiscard]] alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
  parse(const ParsingRequirements& requirements) override;

  std::string_view
  getName() const override
  {
    return m_compilerType;
  }

  // public to allow std::make_unique in create(), use create() to construct
  ClangParser(std::vector<ParseCommand> parseCmds,
              std::vector<std::string> directHeaders,
              std::unique_ptr<clang::tooling::CompilationDatabase> fallbackDb,
              std::vector<std::string> globalIncludes,
              std::string compilerType,
              std::unordered_set<std::string> targetHeaders)
    : m_parseCmds(std::move(parseCmds)),
      m_directHeaders(std::move(directHeaders)),
      m_fallbackDb(std::move(fallbackDb)),
      m_globalIncludes(std::move(globalIncludes)),
      m_compilerType(std::move(compilerType)),
      m_targetHeaders(std::move(targetHeaders))
  {
  }

private:
  std::vector<ParseCommand> m_parseCmds;
  std::vector<std::string> m_directHeaders;  // standalone headers (no TU found)
  std::unique_ptr<clang::tooling::CompilationDatabase> m_fallbackDb;
  std::vector<std::string> m_globalIncludes;  // union of all -I/-isystem paths
  std::string m_compilerType;
  std::unordered_set<std::string> m_targetHeaders;

  static alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>
  loadCompilationDatabase(const std::filesystem::path& buildDir);

  // resolve all user source files to ParseCommands using dep graph pairing.
  // populates outTargetHeaders with the union of all user header paths.
  // outDirectHeaders receives headers/sources for which no TU was found
  // (fallback path).
  static alchemy::core::Result<std::vector<ParseCommand>>
  resolveParseCommands(
      const std::vector<std::filesystem::path>& sourceFiles,
      const std::vector<clang::tooling::CompileCommand>& allCommands,
      const ReverseDependencyMap& reverseDepMap,
      std::unordered_set<std::string>& outTargetHeaders,
      std::vector<std::string>& outDirectHeaders);

  // run an isolated ClangTool for one ParseCommand (thin single-command DB).
  alchemy::core::Result<std::vector<alchemy::parser::artifacts::StructDef>>
  run(const ParseCommand& cmd) const;

  // run ClangTool for a standalone header using the global infer fallback DB.
  alchemy::core::Result<std::vector<alchemy::parser::artifacts::StructDef>>
  run(const std::string& header) const;

  // remove duplicate structs.
  // dedup key: sourceFile + "::" + structName
  // (target-agnostic: same struct from multiple compilation contexts counts
  // once)
  static void
  deduplicateStructs(
      std::vector<alchemy::parser::artifacts::StructDef>& structs);

  // build reverse dep map: for each TU command, run clang -MM to get exact
  // headers included; populate header path → [TranslationUnit] reverse map.
  static ReverseDependencyMap
  buildReverseDependencyMap(
      const std::vector<clang::tooling::CompileCommand>& allCommands,
      const std::string& clangBinary);

  // process one compile command's -MM output into includedBy (thread-safe:
  // caller owns includedBy exclusively).
  static void
  buildParseGraph(const clang::tooling::CompileCommand& cmd,
                  const std::string& clangBinary,
                  ReverseDependencyMap& includedBy);

  // filter compile commands whose source file matches an exclude pattern.
  static std::vector<clang::tooling::CompileCommand>
  filterExcludedCommands(
      const std::vector<clang::tooling::CompileCommand>& allCommands,
      const std::vector<std::string>& excludePatterns);

  static std::vector<std::string>
  resolveStdPreamble(const std::unordered_set<std::string>& headerFiles,
                     const std::vector<std::string>& includePaths);

  static void
  injectIncludePaths(clang::tooling::ClangTool& tool,
                     const std::vector<std::string>& dbFiles,
                     std::vector<std::string> allIncludes);

  static void
  injectStdPreamble(clang::tooling::ClangTool& tool,
                    std::unordered_set<std::string> headerFiles,
                    std::vector<std::string> preamble);
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_PARSER_HPP
