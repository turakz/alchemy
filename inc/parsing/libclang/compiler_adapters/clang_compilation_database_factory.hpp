// inc/parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_CLANG_COMPILATION_DATABASE_FACTORY_HPP
#define ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_CLANG_COMPILATION_DATABASE_FACTORY_HPP
// std
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

namespace alchemy::parser::libclang::adapters {

struct CompilationDatabaseInfo {
  std::unique_ptr<clang::tooling::CompilationDatabase> database;
  std::string compilerType;
  std::vector<std::string> allIncludePaths;
};

class CompilationDatabaseFactory {
public:
  static alchemy::core::Result<CompilationDatabaseInfo>
  fromBuildDir(const std::filesystem::path& buildDir);
};

}  // namespace alchemy::parser::libclang::adapters
#endif
