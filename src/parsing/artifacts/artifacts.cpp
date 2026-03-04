// src/parsing/artifacts/artifacts.cpp
// simulate alignment operation as if computer were allocating memory
// std
#include <cstddef>

#include <numeric>
#include <vector>

// 3rd party

// local
#include "parsing/artifacts/artifacts.hpp"

std::size_t
alchemy::parser::artifacts::StructDef::computeSize(
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
    std::size_t alignment)
{
  std::size_t offset = 0;
  for (const auto& field : fields)
  {
    // address is not a multiple of sizeof(field)
    if ((field.naturalAlignment > 0) && (offset % field.naturalAlignment != 0))
    {
      const std::size_t Padding =
          field.naturalAlignment - (offset % field.naturalAlignment);
      offset += Padding;
    }

    // place at address which is a multiple of sizeof(field)
    offset += field.naturalSize;
  }
  // add tail padding
  if ((alignment > 0) && (offset % alignment != 0))
  {
    const std::size_t Padding = alignment - (offset % alignment);
    offset += Padding;
  }
  // struct size
  return offset;
}

std::size_t
alchemy::parser::artifacts::StructDef::computeDataSize(
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields)
{
  return std::accumulate(std::begin(fields),
                         std::end(fields),
                         std::size_t{0},
                         [](std::size_t sum, const auto& field) {
                           return sum + field.naturalSize;
                         });
}
