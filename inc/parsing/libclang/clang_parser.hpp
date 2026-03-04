// inc/clang_parser.hpp
#ifndef ALCHEMY_PARSING_CLANG_PARSER_HPP
#define ALCHEMY_PARSING_CLANG_PARSER_HPP

// std
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/parser.hpp"
#include "parsing/parsing_requirements.hpp"

namespace alchemy::parser {

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
                       std::string compilerType = "GCC/Clang")
    : m_tool(std::move(tool)),
      m_compilationDb(std::move(compilationDb)),
      m_compilerType(std::move(compilerType))
  {
  }

  [[nodiscard]] alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
  parse(const ParsingRequirements& requirements) override;

  std::string_view
  getName() const override
  {
    return m_compilerType;
  }

  std::vector<std::string_view>
  getSupportedExtensions() const override
  {
    return {".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".cxx", ".hxx"};
  }

private:
  std::unique_ptr<clang::tooling::ClangTool> m_tool;
  std::unique_ptr<clang::tooling::CompilationDatabase> m_compilationDb;
  std::string m_compilerType;
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_PARSER_HPP
