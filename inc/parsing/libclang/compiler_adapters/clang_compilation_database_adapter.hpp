// inc/parsing/libclang/compiler_adapters/clang_compilation_database_adapter.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_CLANG_COMPILATION_DATABASE_ADAPTER_HPP
#define ALCHEMY_PARSING_LIBCLANG_CLANG_COMPILATION_DATABASE_ADAPTER_HPP
// std
#include <string>
#include <unordered_map>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/ADT/StringRef.h>

// local

namespace alchemy::parser::libclang::adapters {

class ClangCompilationDatabaseAdapter
  : public clang::tooling::CompilationDatabase {
public:
  explicit ClangCompilationDatabaseAdapter(
      std::vector<clang::tooling::CompileCommand> commands);

  // intercept clang::tooling::CompilationDatabase calls and adapt on fly
  std::vector<clang::tooling::CompileCommand>
  getCompileCommands(llvm::StringRef filePath) const override;

  std::vector<clang::tooling::CompileCommand>
  getAllCompileCommands() const override;

  std::vector<std::string>
  getAllFiles() const override;

private:
  std::vector<clang::tooling::CompileCommand> m_allCommands;
  std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>>
      m_commandsByFile;
};

}  // namespace alchemy::parser::libclang::adapters
#endif
