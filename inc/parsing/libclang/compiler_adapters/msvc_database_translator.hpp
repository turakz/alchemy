// inc/parsing/libclang/compiler_adapters/msvc_database_translator.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_MSVC_DATABASE_TRANSLATOR_HPP
#define ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_MSVC_DATABASE_TRANSLATOR_HPP
// std
#include <string>
#include <unordered_set>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local

namespace alchemy::parser::libclang::adapters {

class MsvcDbTranslator {
public:
  clang::tooling::CompileCommand
  translateCommand(const clang::tooling::CompileCommand& msvcCommand) const;
  static bool
  isMsvcCompiler(const clang::tooling::CompilationDatabase& db);
  static const std::unordered_set<std::string>&
  knownCompilerFlags();
};

}  // namespace alchemy::parser::libclang::adapters
#endif
