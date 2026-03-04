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

struct IarQueryConfig {
  std::string compilerPath;
  std::vector<std::string> archFlags;
  auto
  operator<=>(const IarQueryConfig&) const = default;
};

class IarDbTranslator {
public:
  explicit IarDbTranslator(const std::vector<std::string>& sysIncludes,
                           const std::vector<std::string>& sysDefines)
    : m_sysIncludes(sysIncludes), m_sysDefines(sysDefines)
  {
  }
  /// translate a single IAR CompileCommand to a clang-compatible command
  clang::tooling::CompileCommand
  translateCommand(const clang::tooling::CompileCommand& iarCommand) const;
  /// detect if a compilation database appears to use IAR compiler
  static bool
  isIarCompiler(const clang::tooling::CompilationDatabase& db);
  /// query known IAR flags to strip
  static alchemy::core::Result<IarQueryConfig>
  extractQueryConfig(const clang::tooling::CompilationDatabase& db);
  static alchemy::core::Result<std::vector<std::string>>
  querySystemIncludes(const IarQueryConfig& query);
  static alchemy::core::Result<std::vector<std::string>>
  querySystemDefines(const IarQueryConfig& query);
  static const std::unordered_set<std::string>&
  knownCompilerFlags();

private:
  std::vector<std::string> m_sysIncludes;
  std::vector<std::string> m_sysDefines;
  static std::vector<std::string>
  parseSystemIncludes(const std::string& compilerOutput);
  static alchemy::core::Result<std::vector<std::string>>
  parseSystemDefines(const std::filesystem::path& predefFile);
  static std::vector<std::string>
  getClangCompatibleDefines();
};

}  // namespace alchemy::parser::libclang::adapters
#endif
