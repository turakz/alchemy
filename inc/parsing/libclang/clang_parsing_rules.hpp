// inc/clang_parsing_rules.hpp
#ifndef ALCHEMY_PARSING_CLANG_PARSING_RULES_HPP
#define ALCHEMY_PARSING_CLANG_PARSING_RULES_HPP

// std
#include <string>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>

namespace alchemy::parser {

// provides interface for registering matchers and handling AST callbacks
class ClangParsingMatcher
  : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  ~ClangParsingMatcher() override = default;
  ClangParsingMatcher() = default;
  ClangParsingMatcher(const ClangParsingMatcher&) = delete;
  ClangParsingMatcher&
  operator=(const ClangParsingMatcher&) = delete;
  ClangParsingMatcher(ClangParsingMatcher&&) = default;
  ClangParsingMatcher&
  operator=(ClangParsingMatcher&&) = default;

  // register AST matchers with finder
  virtual void
  registerMatchers(clang::ast_matchers::MatchFinder& finder) = 0;

  // matcher callback (inherited from MatchFinder::MatchCallback)
  void
  run(const clang::ast_matchers::MatchFinder::MatchResult& result) override = 0;

  // metadata
  virtual std::string
  getName() const = 0;
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_PARSING_RULES_HPP
