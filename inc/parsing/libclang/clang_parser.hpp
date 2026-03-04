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

struct ParseTargetResult {
  std::vector<std::string> filesToParse;
  std::unordered_set<std::string> targetHeaders;
  std::unordered_set<std::string> headerFiles;
  std::size_t pairedCount = 0;
  std::size_t directCount = 0;
  std::size_t sourceCount = 0;
};

// clang-specific parser adapter
// wraps clang::tooling::ClangTool and orchestrates AST parsing
class ClangParser : public ParsingRuleAdapter {
public:
  // factory method: creates ClangParser with ClangTool from source files
  // -> handles LLVM/Clang tooling setup (CommonOptionsParser, compilation
  // database)
  [[nodiscard]] static alchemy::core::Result<
      std::unique_ptr<alchemy::parser::ClangParser>>
  create(const std::vector<std::filesystem::path>& sourceFiles,
         const std::filesystem::path& buildDir);

  // direct constructor (use create() for automatic LLVM setup)
  explicit ClangParser(std::unique_ptr<clang::tooling::ClangTool> tool,
                       std::unique_ptr<clang::tooling::CompilationDatabase>
                           compilationDb = nullptr,
                       std::string compilerType = "Clang",
                       std::unordered_set<std::string> targetHeaders = {})
    : m_tool(std::move(tool)),
      m_compilationDb(std::move(compilationDb)),
      m_compilerType(std::move(compilerType)),
      m_targetHeaders(std::move(targetHeaders))
  {
  }

  [[nodiscard]] alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
  parse(const ParsingRequirements& requirements) override;

  std::string_view
  getName() const override
  {
    return m_compilerType;
  }

private:
  std::unique_ptr<clang::tooling::ClangTool> m_tool;
  std::unique_ptr<clang::tooling::CompilationDatabase> m_compilationDb;
  std::string m_compilerType;
  std::unordered_set<std::string> m_targetHeaders;

  // create() decomposition helpers
  static alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>
  loadCompilationDatabase(const std::filesystem::path& buildDir);

  static std::unordered_map<std::string, std::vector<std::string>>
  buildTranslationUnitIndex(const std::vector<std::string>& dbFiles);

  static alchemy::parser::ParseTargetResult
  resolveParseTargets(
      const std::vector<std::filesystem::path>& sourceFiles,
      const std::unordered_map<std::string, std::vector<std::string>>& tuIndex);

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
