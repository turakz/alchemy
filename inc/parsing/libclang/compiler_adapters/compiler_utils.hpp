// inc/parsing/libclang/compiler_adapters/compiler_utils.hpp
#ifndef ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_COMPILER_UTILS_HPP
#define ALCHEMY_PARSING_LIBCLANG_COMPILER_ADAPTERS_COMPILER_UTILS_HPP
// std
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 3rd party
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

// cross-compiler database translation: compiler-queried system include
// directories must be added as -isystem, not -I. clang's search order is:
//   1. -I directories (project includes)
//   2. clang resource directory (stdbool.h, stdint.h, stddef.h — builtins)
//   3. -isystem directories (compiler-queried system includes)
// using -I would cause clang to find the vendor compiler's std headers before
// its own resource directory versions. vendor headers (IAR, GCC) are not
// clang-compatible, but clang's resource directory headers use compiler
// builtins that are target-correct regardless of cross-compilation target.
// -isystem ensures non-standard vendor headers (intrinsics, HAL, CMSIS)
// remain reachable while standard headers resolve from clang's resource dir.
namespace alchemy::parser::libclang::adapters {

// execute a compiler command and capture its combined stdout+stderr output.
// used by GCC and IAR translators to query system includes, target triples,
// etc.
alchemy::core::Result<std::string>
executeCompilerCommand(const std::string& compiler,
                       const std::vector<std::string>& args,
                       std::string_view callerContext,
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

// parse -I and -isystem flag values from an argument list.
// returns the set of all include path values (stripped of flag prefix).
std::unordered_set<std::string>
parseIncludeFlags(const std::vector<std::string>& args);

// find insertion point in an argument list: before "--" separator if
// present, otherwise before the last element (the source file).
// ClangTool places "--" before the source filename, and anything after
// "--" is treated as a positional filename.
std::vector<std::string>::iterator
findArgInsertionPoint(std::vector<std::string>& args);

// extract build target name from a CompileCommand::Output field.
// tries known build system patterns:
//   - CMake: .../CMakeFiles/<target>.dir/... → "<target>"
// returns "default" if the output field is empty or no pattern matches.
// extend by adding new pattern cases to the implementation.
std::string
extractBuildTarget(const std::string& outputField);

// group translated CompileCommands by build target name.
// uses extractBuildTarget() on each command's Output field.
// commands without a recognizable target are grouped under "default".
std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>>
groupCommandsByTarget(
    const std::vector<clang::tooling::CompileCommand>& commands);

// find a working clang binary.
// probes: "clang", then the versioned aliases installed by 'make setup':
//   clang-20 (Ubuntu 26.04), clang-18 (24.04), clang-14 (22.04),
//   clang-12 (20.04). macOS receives unversioned clang via Homebrew.
// hard-errors if nothing responds — alchemy requires clang to be installed.
// run 'make setup' or 'make doctor' if this fails.
alchemy::core::Result<std::string>
findClangBinary();

}  // namespace alchemy::parser::libclang::adapters
#endif
