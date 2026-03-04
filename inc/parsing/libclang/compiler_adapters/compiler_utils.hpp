// inc/parsing/libclang/compiler_adapters/compiler_utils.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_COMPILER_UTILS_HPP
#define ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_COMPILER_UTILS_HPP
// std
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

// 3rd party
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

namespace alchemy::parser::libclang::adapters {

// execute a compiler command and capture its combined stdout+stderr output.
// used by GCC and IAR translators to query system includes, target triples,
// etc.
alchemy::core::Result<std::string>
executeCompilerCommand(const std::string& compiler,
                       const std::vector<std::string>& args,
                       const std::string& callerContext,
                       bool allowNonZeroExit = false);

// translate all commands in a compilation database using a translator.
// translator must have: translateCommand(const CompileCommand&) const
template <typename TranslatorT>
std::vector<clang::tooling::CompileCommand>
translateDb(const TranslatorT& translator,
            const clang::tooling::CompilationDatabase& db)
{
  auto commands = db.getAllCompileCommands();
  std::vector<clang::tooling::CompileCommand> result;
  result.reserve(commands.size());
  std::transform(std::begin(commands),
                 std::end(commands),
                 std::back_inserter(result),
                 [&translator](const auto& cmd) {
                   return translator.translateCommand(cmd);
                 });
  return result;
}

// extract the union of all -I and -isystem paths from translated commands,
// filtering to paths that exist on disk. used to augment inferred commands
// for headers not in the database.
std::vector<std::string>
extractIncludePaths(
    const std::vector<clang::tooling::CompileCommand>& commands);

// return -I flags for include paths not already present in args
std::vector<std::string>
inferMissingIncludeFlags(const clang::tooling::CommandLineArguments& args,
                         const std::vector<std::string>& allIncludes);

// find insertion point in an argument list: before "--" separator if
// present, otherwise before the last element (the source file).
// ClangTool places "--" before the source filename, and anything after
// "--" is treated as a positional filename.
std::vector<std::string>::iterator
findArgInsertionPoint(std::vector<std::string>& args);

}  // namespace alchemy::parser::libclang::adapters
#endif
