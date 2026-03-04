// src/clang_struct_extractor.cpp
#include "parsing/libclang/clang_struct_extractor.hpp"

// std
#include <cctype>
#include <cstddef>

#include <string>
#include <utility>

// 3rd party
#include <clang/AST/Attr.h>
#include <clang/AST/Attrs.inc>
#include <clang/AST/CharUnits.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/StringRef.h>

// local
#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"

// source buffer scanning

unsigned
alchemy::parser::StructExtractor::skipBackwardHorizWs(llvm::StringRef buf,
                                                      unsigned pos)
{
  while (pos > 0 && (buf[pos - 1] == ' ' || buf[pos - 1] == '\t'))
    --pos;
  return pos;
}

unsigned
alchemy::parser::StructExtractor::skipForwardHorizWs(llvm::StringRef buf,
                                                     unsigned pos)
{
  const auto size = static_cast<unsigned>(buf.size());
  while (pos < size && (buf[pos] == ' ' || buf[pos] == '\t'))
    ++pos;
  return pos;
}

unsigned
alchemy::parser::StructExtractor::skipBackwardNewline(llvm::StringRef buf,
                                                      unsigned pos)
{
  if (pos > 0 && buf[pos - 1] == '\n')
    --pos;
  if (pos > 0 && buf[pos - 1] == '\r')
    --pos;
  return pos;
}

unsigned
alchemy::parser::StructExtractor::findLineStart(llvm::StringRef buf,
                                                unsigned pos)
{
  while (pos > 0 && buf[pos - 1] != '\n')
    --pos;
  return pos;
}

// field byte offset correction

unsigned
alchemy::parser::StructExtractor::computeFieldByteOffset(
    const clang::FieldDecl* fieldDecl,
    const clang::SourceManager& sourceManager,
    clang::SourceLocation beginPos,
    clang::FileID fieldFileID,
    llvm::StringRef fieldBuf)
{
  const unsigned ByteOffset = sourceManager.getFileOffset(beginPos);
  const bool BeginInFieldFile =
      (sourceManager.getFileID(beginPos) == fieldFileID);

  if (BeginInFieldFile)
  {
    return ByteOffset;
  }

  // BeginPos is in the wrong file (BuiltinTypeLoc bug for _Bool/bool).
  // Scan backward from the identifier location to find the type keyword start.
  const unsigned IdentOffset =
      sourceManager.getFileOffset(fieldDecl->getLocation());
  // guard: identifier offset can exceed the buffer for compiler-internal types
  // (e.g., ARM __va_list defined in clang's builtin definitions buffer)
  if (IdentOffset >= static_cast<unsigned>(fieldBuf.size()))
  {
    return ByteOffset;
  }

  unsigned pos = skipBackwardHorizWs(fieldBuf, IdentOffset);

  // skip backward past type keyword (alphanumeric, _, *, whitespace)
  // stop at newline, semicolon, brace, or other delimiter
  while (pos > 0)
  {
    const char Ch = fieldBuf[pos - 1];
    if (std::isalnum(static_cast<unsigned char>(Ch)) != 0 || Ch == '_' ||
        Ch == '*' || Ch == ' ' || Ch == '\t')
    {
      --pos;
    }
    else
    {
      break;
    }
  }

  // skip forward past indentation (we want the type keyword, not whitespace)
  return skipForwardHorizWs(fieldBuf, pos);
}

// trailing comment extraction

alchemy::parser::StructExtractor::TrailingCommentInfo
alchemy::parser::StructExtractor::extractTrailingComment(
    const clang::SourceManager& sourceManager,
    const clang::LangOptions& langOpts,
    clang::SourceLocation endPos,
    unsigned fieldByteOffset)
{
  TrailingCommentInfo result;

  // find the semicolon after the field declaration
  result.semiLoc = clang::Lexer::findLocationAfterToken(
      endPos, clang::tok::semi, sourceManager, langOpts, false);

  if (!result.semiLoc.isValid())
  {
    // fallback: just the field declaration without semicolon
    result.byteLength = sourceManager.getFileOffset(endPos) - fieldByteOffset;
    return result;
  }

  const unsigned SemiEnd = sourceManager.getFileOffset(result.semiLoc);
  result.byteLength = SemiEnd - fieldByteOffset;

  // extend byte range to include trailing same-line comments so they are
  // consumed during field reordering — the comment text is captured in
  // result.text and travels with the field to its new position
  // note: use endPos (not beginPos) for getFileID — beginPos can be bogus
  // for builtin types like _Bool/bool (clang BuiltinTypeLoc bug), pointing
  // to the wrong file buffer. endPos is always in the correct source file.
  const llvm::StringRef FileContent =
      sourceManager.getBufferData(sourceManager.getFileID(endPos));
  const auto FileSize = static_cast<unsigned>(FileContent.size());
  unsigned scanPos = skipForwardHorizWs(FileContent, SemiEnd);

  // remember where the comment starts (if any)
  const unsigned CommentStart = scanPos;

  // C-style block comment: /* ... */ or /**< ... */
  if (scanPos + 1 < FileSize && FileContent[scanPos] == '/' &&
      FileContent[scanPos + 1] == '*')
  {
    scanPos += 2;
    while (scanPos + 1 < FileSize)
    {
      if (FileContent[scanPos] == '*' && FileContent[scanPos + 1] == '/')
      {
        scanPos += 2;
        break;
      }
      ++scanPos;
    }
    result.byteLength = scanPos - fieldByteOffset;
    result.text =
        FileContent.substr(CommentStart, scanPos - CommentStart).str();
  }
  // C++ line comment: // ...
  else if (scanPos + 1 < FileSize && FileContent[scanPos] == '/' &&
           FileContent[scanPos + 1] == '/')
  {
    while (scanPos < FileSize && FileContent[scanPos] != '\n')
    {
      ++scanPos;
    }
    result.byteLength = scanPos - fieldByteOffset;
    result.text =
        FileContent.substr(CommentStart, scanPos - CommentStart).str();
  }

  return result;
}

// source type info extraction

alchemy::parser::StructExtractor::SourceTypeInfo
alchemy::parser::StructExtractor::extractSourceTypeInfo(
    const clang::FieldDecl* fieldDecl,
    const clang::SourceManager& sourceManager,
    llvm::StringRef fieldBuf,
    unsigned fieldByteOffset,
    const std::string& fieldName,
    clang::SourceLocation semiLoc)
{
  SourceTypeInfo result;

  const unsigned IdentOffset =
      sourceManager.getFileOffset(fieldDecl->getLocation());
  const auto TypeBufSize = static_cast<unsigned>(fieldBuf.size());

  // guard: offsets can exceed the buffer for compiler-internal types
  if (fieldByteOffset >= TypeBufSize || IdentOffset >= TypeBufSize ||
      fieldByteOffset > IdentOffset)
  {
    return result;  // empty typeName and arraySuffix — use canonical fallback
  }

  // extract source-faithful type name
  // (clang's getAsString() returns canonical names like "_Bool" instead of
  // "bool" and resolves array macros like "[SIZE]" to "[11]")
  result.typeName =
      fieldBuf.substr(fieldByteOffset, IdentOffset - fieldByteOffset).str();

  // trim trailing whitespace (between type keyword and identifier)
  auto lastNonSpace = result.typeName.find_last_not_of(" \t");
  if (lastNonSpace != std::string::npos)
  {
    result.typeName.erase(lastNonSpace + 1);
  }

  // extract source-faithful array suffix (e.g., "[SIZE]", "[3][76]")
  // read from after the identifier to the semicolon
  if (semiLoc.isValid())
  {
    const unsigned IdentEnd =
        IdentOffset + static_cast<unsigned>(fieldName.length());
    const unsigned SemiOffset =
        sourceManager.getFileOffset(semiLoc) - 1;  // position of ';'
    if (SemiOffset > IdentEnd)
    {
      result.arraySuffix =
          fieldBuf.substr(IdentEnd, SemiOffset - IdentEnd).str();
      // trim whitespace
      auto firstNonSpace = result.arraySuffix.find_first_not_of(" \t");
      if (firstNonSpace != std::string::npos &&
          result.arraySuffix[firstNonSpace] == '[')
      {
        result.arraySuffix = result.arraySuffix.substr(firstNonSpace);
      }
      else
      {
        result.arraySuffix.clear();  // no array brackets found
      }
    }
  }

  return result;
}

// preceding comment detection strategies

alchemy::core::Result<unsigned>
alchemy::parser::StructExtractor::findPrecedingLineComment(
    llvm::StringRef buf, unsigned prevLineStart, std::size_t firstNonSpace)
{
  unsigned commentStart = prevLineStart + static_cast<unsigned>(firstNonSpace);

  // walk further back for consecutive line comments (multi-line blocks)
  unsigned checkPos = prevLineStart;
  while (checkPos > 0)
  {
    unsigned prevEnd = skipBackwardNewline(buf, checkPos);
    if (prevEnd == 0)
    {
      break;
    }

    unsigned lineStart = findLineStart(buf, prevEnd);

    const llvm::StringRef Line = buf.substr(lineStart, prevEnd - lineStart);
    const auto Ws = Line.find_first_not_of(" \t");

    if (Ws != llvm::StringRef::npos && Line.size() >= Ws + 2 &&
        Line[Ws] == '/' && Line[Ws + 1] == '/')
    {
      commentStart = lineStart + static_cast<unsigned>(Ws);
      checkPos = lineStart;
    }
    else
    {
      break;
    }
  }

  return alchemy::core::Result<unsigned>::success(commentStart);
}

alchemy::core::Result<unsigned>
alchemy::parser::StructExtractor::findPrecedingBlockComment(
    llvm::StringRef buf, unsigned prevLineStart, std::size_t lastContent)
{
  const unsigned EndStar =
      prevLineStart + static_cast<unsigned>(lastContent) - 1;

  // scan backward from * in */ to find opening /*
  unsigned searchPos = EndStar;
  while (searchPos >= 1)
  {
    if (buf[searchPos - 1] == '/' && buf[searchPos] == '*')
    {
      const unsigned OpenSlash = searchPos - 1;

      // find start of line containing /*
      unsigned lineStart = findLineStart(buf, OpenSlash);

      // skip indentation to find comment marker
      unsigned commentStart = skipForwardHorizWs(buf, lineStart);

      // verify /* is the first non-whitespace on its line — if not,
      // it's a trailing comment for a field declaration (e.g.,
      // "char x; /* ... */"), not a standalone preceding comment
      if (commentStart != OpenSlash)
      {
        break;
      }

      return alchemy::core::Result<unsigned>::success(commentStart);
    }
    --searchPos;
  }

  return alchemy::core::Result<unsigned>::failure("");
}

// preceding comment extraction

alchemy::parser::StructExtractor::PrecedingCommentInfo
alchemy::parser::StructExtractor::extractPrecedingComment(
    llvm::StringRef fieldBuf, unsigned fieldByteOffset, unsigned byteLength)
{
  PrecedingCommentInfo result;
  result.adjustedByteOffset = fieldByteOffset;
  result.adjustedByteLength = byteLength;

  const llvm::StringRef FileBuf = fieldBuf;

  // guard: offset can exceed the buffer for compiler-internal types
  if (fieldByteOffset >= static_cast<unsigned>(FileBuf.size()))
  {
    return result;
  }

  // skip backward past field indentation, then newline to reach preceding line
  unsigned scanPos = skipBackwardHorizWs(FileBuf, fieldByteOffset);
  scanPos = skipBackwardNewline(FileBuf, scanPos);

  const unsigned PrevLineEnd = scanPos;

  if (PrevLineEnd == 0)
  {
    return result;
  }

  // find start of preceding line
  unsigned prevLineStart = findLineStart(FileBuf, PrevLineEnd);

  const llvm::StringRef PrevLine =
      FileBuf.substr(prevLineStart, PrevLineEnd - prevLineStart);
  const auto FirstNonSpace = PrevLine.find_first_not_of(" \t");
  const auto LastContent = PrevLine.find_last_not_of(" \t");

  // detect comment style on the preceding line
  alchemy::core::Result<unsigned> commentResult =
      alchemy::core::Result<unsigned>::failure("");

  // check for line comment (// or ///) as first non-whitespace
  if (FirstNonSpace != llvm::StringRef::npos &&
      PrevLine.size() >= FirstNonSpace + 2 && PrevLine[FirstNonSpace] == '/' &&
      PrevLine[FirstNonSpace + 1] == '/')
  {
    commentResult =
        findPrecedingLineComment(FileBuf, prevLineStart, FirstNonSpace);
  }
  // check for block comment ending with */
  else if (LastContent != llvm::StringRef::npos && LastContent >= 1 &&
           PrevLine[LastContent] == '/' && PrevLine[LastContent - 1] == '*')
  {
    commentResult =
        findPrecedingBlockComment(FileBuf, prevLineStart, LastContent);
  }

  // populate result if a preceding comment was found
  if (commentResult.valid())
  {
    const unsigned commentStart = commentResult.value();
    result.text =
        FileBuf.substr(commentStart, PrevLineEnd - commentStart).str();
    result.commentFieldGap =
        FileBuf.substr(PrevLineEnd, fieldByteOffset - PrevLineEnd).str();
    result.adjustedByteLength += (fieldByteOffset - commentStart);
    result.adjustedByteOffset = commentStart;
  }

  return result;
}

alchemy::core::Result<alchemy::parser::artifacts::FieldDef>
alchemy::parser::StructExtractor::extractField(
    const clang::FieldDecl* fieldDecl,
    const clang::ASTContext& context,
    const clang::SourceManager& sourceManager,
    const clang::LangOptions& langOpts)
{
  // basic field information
  const std::string FieldName = fieldDecl->getNameAsString();
  const clang::QualType FieldType = fieldDecl->getType();
  const std::string TypeName = FieldType.getAsString();

  // source locations
  const clang::SourceLocation BeginPos =
      sourceManager.getExpansionLoc(fieldDecl->getBeginLoc());
  const clang::SourceLocation EndPos = fieldDecl->getEndLoc();
  const clang::FileID FieldFileID =
      sourceManager.getFileID(fieldDecl->getLocation());

  // fetch field file buffer once (used by phases 1, 3, 4)
  const llvm::StringRef FieldBuf = sourceManager.getBufferData(FieldFileID);

  // phase 1: corrected byte offset (BuiltinTypeLoc workaround)
  const unsigned FieldByteOffset = computeFieldByteOffset(
      fieldDecl, sourceManager, BeginPos, FieldFileID, FieldBuf);

  // phase 2: trailing comment + byte length
  auto trailing =
      extractTrailingComment(sourceManager, langOpts, EndPos, FieldByteOffset);

  // phase 3: source-faithful type name + array suffix
  auto sourceType = extractSourceTypeInfo(fieldDecl,
                                          sourceManager,
                                          FieldBuf,
                                          FieldByteOffset,
                                          FieldName,
                                          trailing.semiLoc);

  // phase 4: preceding comment (adjusts byte offset/length)
  auto preceding =
      extractPrecedingComment(FieldBuf, FieldByteOffset, trailing.byteLength);

  // natural type requirements
  const clang::CharUnits SizeInBytes = context.getTypeSizeInChars(FieldType);
  auto naturalSize = static_cast<std::size_t>(SizeInBytes.getQuantity());
  const clang::CharUnits AlignInBytes = context.getTypeAlignInChars(FieldType);
  auto naturalAlignment = static_cast<std::size_t>(AlignInBytes.getQuantity());

  // semantic constraints
  const bool IsBitField = fieldDecl->isBitField();
  const bool HasAnonymousType = TypeName.find("(unnamed") != std::string::npos;
  const bool CanReorder = !IsBitField && !HasAnonymousType;

  // assemble field definition
  alchemy::parser::artifacts::FieldDef fieldDef(preceding.adjustedByteOffset,
                                                preceding.adjustedByteLength,
                                                TypeName,
                                                FieldName,
                                                naturalSize,
                                                naturalAlignment);

  fieldDef.isBitField = IsBitField;
  fieldDef.canReorder = CanReorder;
  fieldDef.trailingComment = std::move(trailing.text);
  fieldDef.sourceTypeName = std::move(sourceType.typeName);
  fieldDef.sourceArraySuffix = std::move(sourceType.arraySuffix);
  fieldDef.precedingComment = std::move(preceding.text);
  fieldDef.commentFieldGap = std::move(preceding.commentFieldGap);

  return alchemy::core::Result<alchemy::parser::artifacts::FieldDef>::success(
      std::move(fieldDef));
}

alchemy::core::Result<alchemy::parser::artifacts::StructDef>
alchemy::parser::StructExtractor::extractStruct(
    const clang::RecordDecl* structDecl,
    const clang::SourceManager& sourceManager,
    const clang::LangOptions& langOpts)
{
  // basic identification (prefer typedef name for anonymous structs)
  std::string structName = structDecl->getNameAsString();
  if (structName.empty())
  {
    if (const auto* typedefDecl = structDecl->getTypedefNameForAnonDecl())
    {
      structName = typedefDecl->getNameAsString();
    }
  }
  const clang::SourceLocation Location = structDecl->getLocation();
  const std::string SourceFile = sourceManager.getFilename(Location).str();

  // create struct definition with basic info
  alchemy::parser::artifacts::StructDef structDef(structName, SourceFile);

  // get layout analysis from Clang
  const clang::ASTContext& context = structDecl->getASTContext();
  const clang::ASTRecordLayout& layout = context.getASTRecordLayout(structDecl);

  // current sizeof -> affected by #pragmapack
  structDef.naturalAlignment =
      static_cast<std::size_t>(layout.getUnadjustedAlignment().getQuantity());

  // analyze semantic constraints
  structDef.isPacked = structDecl->hasAttr<clang::MaxFieldAlignmentAttr>();
  structDef.hasFlexibleArrayMember = structDecl->hasFlexibleArrayMember();

  // extract fields with enhanced metadata
  for (const clang::FieldDecl* fieldDecl : structDecl->fields())
  {
    auto fieldResult =
        extractField(fieldDecl, context, sourceManager, langOpts);
    if (!fieldResult.valid())
    {
      return alchemy::core::Result<alchemy::parser::artifacts::StructDef>::
          failure(std::move(fieldResult).error());
    }

    alchemy::parser::artifacts::FieldDef field = fieldResult.value();

    // update struct-level bitfield detection
    if (field.isBitField)
    {
      structDef.hasBitFields = true;
    }

    structDef.addField(std::move(field));
  }
  structDef.currentDataSize =
      alchemy::parser::artifacts::StructDef::computeDataSize(structDef.fields);
  structDef.naturalTotalSize =
      alchemy::parser::artifacts::StructDef::computeSize(
          structDef.fields, structDef.naturalAlignment);
  structDef.currentWastedBytes =
      structDef.naturalTotalSize - structDef.currentDataSize;

  return alchemy::core::Result<alchemy::parser::artifacts::StructDef>::success(
      std::move(structDef));
}
