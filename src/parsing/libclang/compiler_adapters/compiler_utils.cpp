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

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"

alchemy::core::Result<std::string>
alchemy::parser::libclang::adapters::executeCompilerCommand(
    const std::string& compiler,
    const std::vector<std::string>& args,
    const std::string& callerContext,
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
        alchemy::core::Error::format(callerContext,
                                     "command failed with exit code {}: {}",
                                     returnCode,
                                     cmd));
  }

  return alchemy::core::Result<std::string>::success(std::move(output));
}

std::vector<std::string>
alchemy::parser::libclang::adapters::extractIncludePaths(
    const std::vector<clang::tooling::CompileCommand>& commands)
{
  std::unordered_set<std::string> includePathSet;
  for (const auto& cmd : commands)
  {
    for (std::size_t i = 0; i < cmd.CommandLine.size(); ++i)
    {
      const auto& arg = cmd.CommandLine[i];
      if ((arg == "-I" || arg == "-isystem") && i + 1 < cmd.CommandLine.size())
      {
        includePathSet.insert(cmd.CommandLine[i + 1]);
      }
      else if (arg.starts_with("-I") && arg.size() > 2)
      {
        includePathSet.insert(arg.substr(2));
      }
    }
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
  std::unordered_set<std::string> existingIncludes;
  for (std::size_t i = 0; i < args.size(); ++i)
  {
    if ((args[i] == "-I" || args[i] == "-isystem") && i + 1 < args.size())
    {
      existingIncludes.insert(args[i + 1]);
    }
    else if (args[i].starts_with("-I") && args[i].size() > 2)
    {
      existingIncludes.insert(args[i].substr(2));
    }
  }

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
