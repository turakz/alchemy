// src/parsing/libclang/compiler_adapters/compiler_utils.cpp
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"

// std
#include <cstddef>

// POSIX (popen/pclose not in standard C++)
// NOLINTBEGIN(misc-include-cleaner, modernize-deprecated-headers,
// hicpp-deprecated-headers)
#include <stdio.h>
// NOLINTEND(misc-include-cleaner, modernize-deprecated-headers,
// hicpp-deprecated-headers)

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "logger/logger.hpp"

alchemy::core::Result<std::string>
alchemy::parser::libclang::adapters::executeCompilerCommand(
    const std::string& compiler,
    const std::vector<std::string>& args,
    std::string_view callerContext,
    bool allowNonZeroExit)
{
  std::string cmd = compiler;
  for (const auto& arg : args)
  {
    cmd += " ";
    cmd += arg;
  }
  cmd += " 2>&1";  // redirect stderr to stdout

  std::array<char, 256> buffer{};
  std::string output;
  auto pipeDeleter = [](FILE* f) {
    if (f != nullptr)
    {
      pclose(f);
    };
  };
  // NOLINTNEXTLINE(cert-env33-c): intentionally executing compiler from
  // compilation database
  std::unique_ptr<FILE, decltype(pipeDeleter)> pipe(
      popen(cmd.c_str(), "r"),  // NOLINT(cert-env33-c)
      pipeDeleter);

  if (!pipe)
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format(
            callerContext, "failed to execute command: {}", cmd));
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
  {
    output += buffer.data();
  }

  int returnCode =
      pclose(pipe.release());  // release so unique_ptr doesn't close again
  if (returnCode != 0 && !allowNonZeroExit)
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format(
            callerContext, "command failed with exit code {}", returnCode));
  }

  return alchemy::core::Result<std::string>::success(std::move(output));
}

std::unordered_set<std::string>
alchemy::parser::libclang::adapters::parseIncludeFlags(
    const std::vector<std::string>& args)
{
  std::unordered_set<std::string> includes;
  for (std::size_t i = 0; i < args.size(); ++i)
  {
    if ((args[i] == "-I" || args[i] == "-isystem") && i + 1 < args.size())
    {
      includes.insert(args[i + 1]);
    }
    else if (args[i].starts_with("-I") && args[i].size() > 2)
    {
      includes.insert(args[i].substr(2));
    }
  }
  return includes;
}

std::vector<std::string>
alchemy::parser::libclang::adapters::extractIncludePaths(
    const std::vector<clang::tooling::CompileCommand>& commands)
{
  std::unordered_set<std::string> includePathSet;
  for (const auto& cmd : commands)
  {
    auto flags = parseIncludeFlags(cmd.CommandLine);
    includePathSet.insert(std::begin(flags), std::end(flags));
  }
  std::vector<std::string> result;
  result.reserve(includePathSet.size());
  for (const auto& path : includePathSet)
  {
    if (std::filesystem::is_directory(path))
    {
      result.push_back(path);
    }
  }
  return result;
}

std::vector<std::string>
alchemy::parser::libclang::adapters::inferMissingIncludeFlags(
    const clang::tooling::CommandLineArguments& args,
    const std::vector<std::string>& allIncludes)
{
  auto existingIncludes = parseIncludeFlags(args);
  std::vector<std::string> result;
  for (const auto& inc : allIncludes)
  {
    if (!existingIncludes.contains(inc))
    {
      result.emplace_back("-I" + inc);
    }
  }
  return result;
}

std::vector<std::string>::iterator
alchemy::parser::libclang::adapters::findArgInsertionPoint(
    std::vector<std::string>& args)
{
  auto it = std::find(std::begin(args), std::end(args), "--");
  if (it == std::end(args) && !args.empty())
  {
    it = std::prev(std::end(args));
  }
  return it;
}

std::string
alchemy::parser::libclang::adapters::extractBuildTarget(
    const std::string& outputField)
{
  if (outputField.empty())
  {
    return "default";
  }

  // CMake: .../CMakeFiles/<target>.dir/...
  static constexpr std::string_view CmakeMarker = "CMakeFiles/";
  static constexpr std::string_view CmakeSuffix = ".dir";

  auto markerPos = outputField.find(CmakeMarker);
  if (markerPos != std::string::npos)
  {
    auto targetStart = markerPos + CmakeMarker.size();
    auto suffixPos = outputField.find(CmakeSuffix, targetStart);
    if (suffixPos != std::string::npos && suffixPos > targetStart)
    {
      return outputField.substr(targetStart, suffixPos - targetStart);
    }
  }

  // add new build system patterns here:
  // Meson: <target>.p/ → "<target>"
  // etc.

  return "default";
}

std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>>
alchemy::parser::libclang::adapters::groupCommandsByTarget(
    const std::vector<clang::tooling::CompileCommand>& commands)
{
  std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>>
      groups;
  for (const auto& cmd : commands)
  {
    auto target = extractBuildTarget(cmd.Output);
    groups[target].push_back(cmd);
  }
  return groups;
}

alchemy::core::Result<std::string>
alchemy::parser::libclang::adapters::findClangBinary()
{
  static constexpr auto Context =
      "alchemy::parser::libclang::adapters::findClangBinary";

  // candidates: unversioned first (macOS Homebrew / manual installs),
  // then the versioned aliases that 'make setup' installs per Ubuntu release.
  static const std::vector<std::string> Candidates = {
      "clang",     // macOS (Homebrew) or any system with a PATH symlink
      "clang-20",  // Ubuntu 26.04
      "clang-18",  // Ubuntu 24.04
      "clang-14",  // Ubuntu 22.04
      "clang-12",  // Ubuntu 20.04
  };

  for (const auto& bin : Candidates)
  {
    if (executeCompilerCommand(bin,
                               {"--version"},
                               Context,
                               /*allowNonZeroExit=*/false)
            .valid())
    {
      alchemy::logger::debug(
          "alchemy::{}parser{}::findClangBinary: resolved '{}'\n",
          alchemy::color::ansi::BoldBrightGreen,
          alchemy::color::ansi::Reset,
          bin);
      return alchemy::core::Result<std::string>::success(bin);
    }
  }

  return alchemy::core::Result<std::string>::failure(
      alchemy::core::Error::format(
          Context,
          "no clang binary found (tried: clang, clang-20, clang-18, "
          "clang-14, clang-12)\n"
          "  alchemy requires clang to be installed\n"
          "  hint: run 'make setup' to install dependencies, or "
          "'make doctor' to diagnose the environment"));
}
