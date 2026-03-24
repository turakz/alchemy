// inc/parsing/libclang/compiler_adapters/iar_database_translator.hpp
#ifndef ALCHEMY_LIBCLANG_COMPILER_ADAPTERS_IAR_DATABASE_TRANSLATOR_HPP
#define ALCHEMY_LIBCLANG_COMPILER_ADAPTERS_IAR_DATABASE_TRANSLATOR_HPP
// std
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

namespace alchemy::parser::libclang::adapters {

struct IARQueryConfig {
  std::string compilerPath;
  std::vector<std::string> archFlags;
  auto
  operator<=>(const IARQueryConfig&) const = default;
};

class IARDbTranslator {
public:
  explicit IARDbTranslator(const std::vector<std::string>& sysIncludes,
                           const std::vector<std::string>& sysDefines)
    : m_sysIncludes(sysIncludes), m_sysDefines(sysDefines)
  {
  }
  /// translate a single IAR CompileCommand to a clang-compatible command
  clang::tooling::CompileCommand
  translateCommand(const clang::tooling::CompileCommand& iarCommand) const;
  /// translate all commands in a compilation database
  std::vector<clang::tooling::CompileCommand>
  translateAll(const clang::tooling::CompilationDatabase& db) const;
  /// detect if a compilation database appears to use IAR compiler
  static bool
  isIARCompiler(const clang::tooling::CompilationDatabase& db);
  /// query known IAR flags to strip
  static alchemy::core::Result<IARQueryConfig>
  extractQueryConfig(const clang::tooling::CompilationDatabase& db);
  static alchemy::core::Result<std::vector<std::string>>
  querySystemIncludes(const IARQueryConfig& query);
  static alchemy::core::Result<std::vector<std::string>>
  querySystemDefines(const IARQueryConfig& query);
  static const std::unordered_set<std::string>&
  knownCompilerFlags();

private:
  std::vector<std::string> m_sysIncludes;
  std::vector<std::string> m_sysDefines;
  static alchemy::core::Result<std::string>
  executeCompiler(const std::string& compiler,
                  const std::vector<std::string>& args);
  static std::vector<std::string>
  parseSystemIncludes(const std::string& compilerOutput);
  static std::vector<std::string>
  parseSystemDefines(const std::filesystem::path& predefFile);
  static std::vector<std::string>
  getClangCompatibileDefines();
};

}  // namespace alchemy::parser::libclang::adapters
#endif
