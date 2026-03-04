// src/clang_struct_extractor.cpp
// std
#include <cstddef>

#include <filesystem>
#include <string>
#include <utility>

// 3rd party
#include "clang/AST/Attr.h"
#include "clang/AST/Attrs.inc"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/Lexer.h"

// local

#include "app/core/core.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_struct_extractor.hpp"

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

  // source representation
  const clang::SourceLocation BeginPos = fieldDecl->getBeginLoc();
  const clang::SourceLocation EndPos = fieldDecl->getEndLoc();

  const unsigned ByteOffset = sourceManager.getFileOffset(BeginPos);

  // find the semicolon after the field declaration
  const clang::SourceLocation SemiLoc = clang::Lexer::findLocationAfterToken(
      EndPos, clang::tok::semi, sourceManager, langOpts, false);

  // byteLength includes field declaration + semicolon only
  // comments/whitespace may be left behind (acceptable for v1)
  unsigned byteLength{0};
  if (SemiLoc.isValid())
  {
    byteLength = sourceManager.getFileOffset(SemiLoc) - ByteOffset;
  }
  else
  {
    // fallback: just the field declaration without semicolon
    byteLength = sourceManager.getFileOffset(EndPos) - ByteOffset;
  }

  // natural type requirements
  const clang::CharUnits SizeInBytes = context.getTypeSizeInChars(FieldType);
  auto naturalSize = static_cast<std::size_t>(SizeInBytes.getQuantity());
  const clang::CharUnits AlignInBytes = context.getTypeAlignInChars(FieldType);
  auto naturalAlignment = static_cast<std::size_t>(AlignInBytes.getQuantity());

  // semantic constraints/bitfield analysis
  const bool IsBitField = fieldDecl->isBitField();
  // determine if field can be safely reordered
  const bool CanReorder = !IsBitField;  // bitfields complicate reordering

  // create enhanced field definition
  alchemy::parser::artifacts::FieldDef fieldDef(ByteOffset,
                                                byteLength,
                                                TypeName,
                                                FieldName,
                                                naturalSize,
                                                naturalAlignment);

  // populate semantic constraints
  fieldDef.isBitField = IsBitField;
  fieldDef.canReorder = CanReorder;

  return alchemy::core::Result<alchemy::parser::artifacts::FieldDef>::success(
      std::move(fieldDef));
}

alchemy::core::Result<alchemy::parser::artifacts::StructDef>
alchemy::parser::StructExtractor::extractStruct(
    const clang::RecordDecl* structDecl,
    const clang::SourceManager& sourceManager,
    const clang::LangOptions& langOpts)
{
  // basic identification
  const std::string StructName = structDecl->getNameAsString();
  const clang::SourceLocation Location = structDecl->getLocation();
  const std::filesystem::path SourceFile =
      sourceManager.getFilename(Location).str();

  // create struct definition with basic info
  alchemy::parser::artifacts::StructDef structDef(StructName, SourceFile);

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
