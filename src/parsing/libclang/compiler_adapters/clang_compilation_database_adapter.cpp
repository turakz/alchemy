// src/parsing/libclang/compiler_adapters/clang_compilation_database_adapter.cpp
#include "parsing/libclang/compiler_adapters/clang_compilation_database_adapter.hpp"

// std
#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/ADT/StringRef.h>

// local

alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter::
    ClangCompilationDatabaseAdapter(
        std::vector<clang::tooling::CompileCommand> commands)
  : m_allCommands(std::move(commands))
{
  for (const auto& cmd : m_allCommands)
  {
    m_commandsByFile[cmd.Filename].push_back(cmd);
  }
}

std::vector<clang::tooling::CompileCommand>
alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter::
    getCompileCommands(llvm::StringRef filePath) const
{
  auto itr = m_commandsByFile.find(filePath.str());
  if (itr != std::end(m_commandsByFile))
  {
    return itr->second;
  }
  return {};
}

std::vector<clang::tooling::CompileCommand>
alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter::
    getAllCompileCommands() const
{
  return m_allCommands;
}

std::vector<std::string>
alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter::
    getAllFiles() const
{
  std::vector<std::string> files;
  files.reserve(m_commandsByFile.size());
  std::transform(std::begin(m_commandsByFile),
                 std::end(m_commandsByFile),
                 std::back_inserter(files),
                 [](const auto& pair) { return pair.first; });
  return files;
}
