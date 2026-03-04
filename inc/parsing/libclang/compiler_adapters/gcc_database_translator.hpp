// inc/parsing/libclang/compiler_adapters/gcc_database_translator.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_GCC_DATABASE_TRANSLATOR_HPP
#define ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_GCC_DATABASE_TRANSLATOR_HPP
// std
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

namespace alchemy::parser::libclang::adapters {

struct GccQueryConfig {
  std::string compilerPath;
  std::string language;      // "c" or "c++"
  std::string targetTriple;  // from -dumpmachine (e.g., "arm-none-eabi")
  auto
  operator<=>(const GccQueryConfig&) const = default;
};

class GccDbTranslator {
public:
  GccDbTranslator() = default;
  explicit GccDbTranslator(const std::vector<std::string>& sysIncludes,
                           const std::string& targetTriple = "");
  clang::tooling::CompileCommand
  translateCommand(const clang::tooling::CompileCommand& gccCommand) const;
  static bool
  isGccCompiler(const clang::tooling::CompilationDatabase& db);
  static alchemy::core::Result<GccQueryConfig>
  extractQueryConfig(const clang::tooling::CompilationDatabase& db);
  static alchemy::core::Result<std::vector<std::string>>
  querySystemIncludes(const GccQueryConfig& query);
  static std::string
  validateTargetTriple(const std::string& gccTriple);
  static alchemy::core::Result<std::string>
  queryTargetTriple(const GccQueryConfig& query);

  static std::vector<std::string>
  parseSystemIncludes(const std::string& compilerOutput);

private:
  std::vector<std::string> m_sysIncludes;
  std::string m_targetTriple;
  static const std::unordered_set<std::string>&
  gccFlags();
  static bool
  isGccFlagPrefix(const std::string& arg);
  static bool
  isArmEabiTarget(const std::string& triple);
  static const std::unordered_map<std::string, std::string>&
  gccToClangTranslation();
};

}  // namespace alchemy::parser::libclang::adapters
#endif
