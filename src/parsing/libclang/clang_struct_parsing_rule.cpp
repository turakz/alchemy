// src/clang_struct_parsing_rule.cpp
#include "parsing/libclang/clang_struct_parsing_rule.hpp"

// std
#include <utility>

// 3rd party
#include <clang/AST/Decl.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>

// local
#include "parsing/libclang/clang_struct_extractor.hpp"

void
alchemy::parser::ClangStructParsingRule::registerMatchers(
    clang::ast_matchers::MatchFinder& finder)
{
  // match struct defs (not just decls)
  // note: isExpansionInMainFile() is intentionally omitted — alchemy parses
  // TUs (not headers directly), so struct declarations in included headers
  // must be matched. post-filtering by target header set happens in
  // ClangParser::parse()
  finder.addMatcher(
      clang::ast_matchers::recordDecl(
          clang::ast_matchers::isStruct(),
          clang::ast_matchers::isDefinition(),  // structs with bodies
          clang::ast_matchers::unless(
              clang::ast_matchers::isImplicit())  // skip compiler-generated
                                                  // structs
          )
          .bind("alchemy::parser::ClangStructParsingRule"),
      this);
}

void
alchemy::parser::ClangStructParsingRule::run(
    const clang::ast_matchers::MatchFinder::MatchResult& result)
{
  const auto* structDecl = result.Nodes.getNodeAs<clang::RecordDecl>(
      "alchemy::parser::ClangStructParsingRule");
  if (result.SourceManager == nullptr)
  {
    m_parseErrors.emplace_back("alchemy::parser::ClangStructParsingRule::run::"
                               "expected source file, got nothing");
    return;
  }
  if (structDecl == nullptr)
  {
    m_parseErrors.emplace_back("alchemy::parser::ClangStructParsingRule::run::"
                               "could not parse struct declaration");
    return;
  }
  auto parseResult = alchemy::parser::StructExtractor::extractStruct(
      structDecl, *result.SourceManager, result.Context->getLangOpts());

  if (parseResult.valid())
  {
    m_parsedStructs.emplace_back(std::move(parseResult).value());
  }
  else
  {
    m_parseErrors.emplace_back("alchemy::parser::ClangStructParsingRule::run::"
                               "extractStruct failed: " +
                               std::move(parseResult).error());
  }
}
