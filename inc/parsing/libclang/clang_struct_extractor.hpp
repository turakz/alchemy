#ifndef ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP
#define ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP

// std
#include <string>
#include <string_view>

// 3rd party
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/StringRef.h>

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

private:
  // buffer-scanning primitives
  static unsigned
  skipBackwardHorizWs(llvm::StringRef buf, unsigned pos);
  static unsigned
  skipForwardHorizWs(llvm::StringRef buf, unsigned pos);
  static unsigned
  skipBackwardNewline(llvm::StringRef buf, unsigned pos);
  static unsigned
  findLineStart(llvm::StringRef buf, unsigned pos);

  // field byte offset correction (BuiltinTypeLoc workaround)
  static unsigned
  computeFieldByteOffset(const clang::FieldDecl* fieldDecl,
                         const clang::SourceManager& sourceManager,
                         clang::SourceLocation beginPos,
                         clang::FileID fieldFileID,
                         llvm::StringRef fieldBuf);

  // trailing comment extraction
  struct ExtractedTrailingComment {
    unsigned byteLength{0};
    std::string text;
    clang::SourceLocation semiLoc;
  };

  static ExtractedTrailingComment
  extractTrailingComment(const clang::SourceManager& sourceManager,
                         const clang::LangOptions& langOpts,
                         clang::SourceLocation endPos,
                         unsigned fieldByteOffset);

  // source-faithful type name and array suffix extraction
  struct ExtractedSourceType {
    std::string typeName;
    std::string arraySuffix;
  };

  static ExtractedSourceType
  extractSourceTypeInfo(const clang::FieldDecl* fieldDecl,
                        const clang::SourceManager& sourceManager,
                        llvm::StringRef fieldBuf,
                        unsigned fieldByteOffset,
                        std::string_view fieldName,
                        clang::SourceLocation semiLoc);

  // preceding comment extraction
  struct ExtractedPrecedingComment {
    unsigned adjustedByteOffset{};
    unsigned adjustedByteLength{};
    std::string text;
    std::string commentFieldGap;
  };

  static ExtractedPrecedingComment
  extractPrecedingComment(llvm::StringRef fieldBuf,
                          unsigned fieldByteOffset,
                          unsigned byteLength);

  // preceding comment detection strategies — each returns the comment start
  // byte offset if the preceding line contains the respective comment style
  static alchemy::core::Result<unsigned>
  findPrecedingLineComment(llvm::StringRef buf,
                           unsigned prevLineStart,
                           std::size_t firstNonSpace);

  static alchemy::core::Result<unsigned>
  findPrecedingBlockComment(llvm::StringRef buf,
                            unsigned prevLineStart,
                            std::size_t lastContent);
};

}  // namespace alchemy::parser
#endif  // ALCHEMY_PARSING_CLANG_STRUCT_EXTRACTOR_HPP
