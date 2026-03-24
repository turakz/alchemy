#ifndef ALCHEMY_PARSING_ARTIFACTS_ARTIFACTS_HPP
#define ALCHEMY_PARSING_ARTIFACTS_ARTIFACTS_HPP

// std
#include <cstddef>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// 3rd party

// local

namespace alchemy::parser::artifacts {

namespace detail {
constexpr std::size_t FieldBufferSz{16};
};  // namespace detail

struct FieldDef {
  // source location (for recipe generation)
  unsigned byteOffset{0};
  unsigned byteLength{0};

  // field identity (for replacement text generation)
  std::string typeName;   // type as string: "int", "char*", etc.
  std::string fieldName;  // field identifier: "count"

  // natural type requirements (optimization constraints)
  std::size_t naturalSize{0};       // bytes this type occupies
  std::size_t naturalAlignment{0};  // alignment this type requires

  // semantic constraints (reordering limitations)
  bool isBitField = false;  // is this a bitfield?
  bool canReorder = true;   // safe to move this field?

  FieldDef(unsigned _byteOffset,
           unsigned _byteLength,
           std::string _typeName,
           std::string _fieldName,
           std::size_t _naturalSize = 0,
           std::size_t _naturalAlignment = 0)
    : byteOffset(_byteOffset),
      byteLength(_byteLength),
      typeName(std::move(_typeName)),
      fieldName(std::move(_fieldName)),
      naturalSize(_naturalSize),
      naturalAlignment(_naturalAlignment)
  {
  }
};
struct StructDef {
  // identification (for user-facing reporting)
  std::string structName;
  std::filesystem::path sourceFile{};

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

  StructDef(std::string _structName, std::filesystem::path _sourceFile)
    : structName(std::move(_structName)), sourceFile(std::move(_sourceFile))
  {
    fields.reserve(alchemy::parser::artifacts::detail::FieldBufferSz);
  }

  void
  addField(FieldDef&& field)
  {
    fields.emplace_back(std::move(field));
  }

  std::size_t
  getFieldCount() const
  {
    return fields.size();
  }

  // analysis helper methods
  bool
  isEmpty() const
  {
    return fields.empty();
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
