#ifndef ALCHEMY_PARSING_ARTIFACTS_ARTIFACTS_HPP
#define ALCHEMY_PARSING_ARTIFACTS_ARTIFACTS_HPP

// std
#include <cstddef>

#include <string>
#include <utility>
#include <vector>

// 3rd party

// local

namespace alchemy::parser::artifacts {

namespace detail {
constexpr std::size_t FieldBufferSz{
    16};  // reserve capacity for typical struct field count
};  // namespace detail

struct FieldDef {
  // source location (for recipe generation)
  unsigned byteOffset{0};
  unsigned byteLength{0};

  // field identity (for replacement text generation)
  std::string canonicalTypeName;  // clang canonical: "_Bool", "uint8_t[11]"
  std::string
      sourceTypeName;     // source-faithful: "bool", "uint8_t" (no array dims)
  std::string fieldName;  // field identifier: "count"

  // natural type requirements (optimization constraints)
  std::size_t naturalSize{0};       // bytes this type occupies
  std::size_t naturalAlignment{0};  // alignment this type requires

  // semantic constraints (reordering limitations)
  bool isBitField = false;  // is this a bitfield?
  bool canReorder = true;   // safe to move this field?

  // trailing same-line comment (e.g., "/**< \brief flag */" or "// flag")
  // empty if no trailing comment — travels with the field during reordering
  std::string trailingComment;

  // preceding comment block (e.g., "/// describes field" or "// line1\n  //
  // line2") empty if no preceding comment — travels with the field during
  // reordering
  std::string precedingComment;

  // gap between preceding comment end and field type start (e.g., "\n  ")
  // only meaningful when precedingComment is non-empty
  std::string commentFieldGap;

  // source-faithful array dimensions (e.g., "[SIZE]", "[3][76]", or "" for
  // non-arrays) extracted from source buffer — preserves macro names unlike
  // canonicalTypeName
  std::string sourceArraySuffix;

  FieldDef(unsigned _byteOffset,
           unsigned _byteLength,
           std::string _canonicalTypeName,
           std::string _fieldName,
           std::size_t _naturalSize = 0,
           std::size_t _naturalAlignment = 0)
    : byteOffset(_byteOffset),
      byteLength(_byteLength),
      canonicalTypeName(std::move(_canonicalTypeName)),
      fieldName(std::move(_fieldName)),
      naturalSize(_naturalSize),
      naturalAlignment(_naturalAlignment)
  {
  }
};
struct StructDef {
  // identification (for user-facing reporting)
  std::string structName;
  std::string sourceFile;

  std::size_t naturalTotalSize{
      0};  // total bytes in terms of types including padding
  std::size_t currentDataSize{0};  // bytes without trailing padding
  std::size_t naturalAlignment{0};
  // struct alignment requirement: alignment of biggest field
  std::size_t currentWastedBytes{0};  // == (naturalTotalSize - currentDataSize)

  std::vector<FieldDef> fields;

  // semantic constraints
  bool isPacked = false;                // was this struct packed in memory?
  bool hasFlexibleArrayMember = false;  // trailing flexible array
  bool hasBitFields = false;  // contains bitfields (complicates reordering)

  StructDef(std::string _structName, std::string _sourceFile)
    : structName(std::move(_structName)), sourceFile(std::move(_sourceFile))
  {
    fields.reserve(alchemy::parser::artifacts::detail::FieldBufferSz);
  }

  void
  addField(FieldDef&& field)
  {
    fields.emplace_back(std::move(field));
  }

  static std::size_t
  computeSize(const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
              std::size_t alignment);
  static std::size_t
  computeDataSize(
      const std::vector<alchemy::parser::artifacts::FieldDef>& fields);
};

// aggregated parse results for requirement-driven parsing
struct ParseResults {
  std::vector<alchemy::parser::artifacts::StructDef> structs;
  // Future extension:
  // std::vector<detail::FunctionDef> functions;
};

}  // namespace alchemy::parser::artifacts
#endif  // ALCHEMY_PARSING_ARTIFACTS_ARTIFACTS_HPP
