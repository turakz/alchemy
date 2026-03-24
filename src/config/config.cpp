// src/config/config.cpp
// std
#include <string>
#include <vector>

// 3rd party

// local
#include "config/config.hpp"

std::vector<std::string>
alchemy::config::SourceInventory::sourceFilesAsStrings() const
{
  std::vector<std::string> result;
  result.reserve(sourceFiles.size());
  for (const auto& path : sourceFiles)
  {
    result.push_back(path.string());
  }
  return result;
}
