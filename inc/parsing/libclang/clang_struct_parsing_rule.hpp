#ifndef ALCHEMY_PARSING_CLANG_STRUCT_PARSING_RULE_HPP
#define ALCHEMY_PARSING_CLANG_STRUCT_PARSING_RULE_HPP

// std
#include <string>
#include <unordered_set>
#include <vector>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>

// local
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_parsing_rules.hpp"

namespace alchemy::parser {

class ClangStructParsingRule : public ClangParsingMatcher {
public:
  explicit ClangStructParsingRule(std::unordered_set<std::string> targetHeaders)
    : m_targetHeaders(std::move(targetHeaders))
  {
  }
  void
  registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
  void
  run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
  std::string
  getName() const override
  {
    return "ClangStructParser";
  };

  const std::vector<parser::artifacts::StructDef>&
  getParsedStructs() const
  {
    return m_parsedStructs;
  };

  // error accumulation for fail-fast behavior
  const std::vector<std::string>&
  getParseErrors() const
  {
    return m_parseErrors;
  }
  bool
  hasParseErrors() const
  {
    return !m_parseErrors.empty();
  }

private:
  std::unordered_set<std::string> m_targetHeaders;
  std::vector<parser::artifacts::StructDef> m_parsedStructs;
  std::vector<std::string> m_parseErrors;
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_STRUCT_PARSING_RULE_HPP
