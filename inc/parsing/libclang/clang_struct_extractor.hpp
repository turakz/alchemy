#ifndef ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP
#define ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP

// std

// 3rd party
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"

namespace alchemy::parser {

// pure extraction logic - separated from Clang AST orchestration
class StructExtractor {
public:
  static core::Result<alchemy::parser::artifacts::FieldDef>
  extractField(const clang::FieldDecl* fieldDecl,
               const clang::ASTContext& context,
               const clang::SourceManager& sourceManager,
               const clang::LangOptions& langOpts);

  static core::Result<alchemy::parser::artifacts::StructDef>
  extractStruct(const clang::RecordDecl* structDecl,
                const clang::SourceManager& sourceManager,
                const clang::LangOptions& langOpts);
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP
