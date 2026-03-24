// src/parsing/libclang/compiler_adapters/iar_database_translator.cpp
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
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"

clang::tooling::CompileCommand
alchemy::parser::libclang::adapters::IARDbTranslator::translateCommand(
    const clang::tooling::CompileCommand& iarCommand) const
{
  clang::tooling::CompileCommand clangCommand = iarCommand;
  std::vector<std::string> translatedArgs;
  translatedArgs.emplace_back("clang");

  for (const auto& path : m_sysIncludes)
  {
    translatedArgs.emplace_back("-isystem");
    translatedArgs.emplace_back(path);
  }
  // add architecture specific defines
  translatedArgs.insert(std::end(translatedArgs),
                        std::begin(m_sysDefines),
                        std::end(m_sysDefines));

  // add IAR compatibility defines (define away keywords)
  auto compatDefines = alchemy::parser::libclang::adapters::IARDbTranslator::
      getClangCompatibileDefines();
  translatedArgs.insert(std::end(translatedArgs),
                        std::begin(compatDefines),
                        std::end(compatDefines));

  // copy standard flags from IAR command (skip IAR-specific ones)
  for (std::size_t idx = 1; idx < iarCommand.CommandLine.size(); ++idx)
  {
    const auto& arg = iarCommand.CommandLine[idx];

    // skip IAR-specific compiler flags
    if (std::any_of(
            alchemy::parser::libclang::adapters::IARDbTranslator::
                knownCompilerFlags()
                    .begin(),
            alchemy::parser::libclang::adapters::IARDbTranslator::
                knownCompilerFlags()
                    .end(),
            [&arg](const std::string& flag) { return arg.starts_with(flag); }))
    {
      continue;
    }

    // keep standard flags (-D, -I, -U, source files)
    translatedArgs.emplace_back(arg);
  }

  // add clang target
  translatedArgs.emplace_back("-target");
  translatedArgs.emplace_back("arm-none-eabi");
  // parsing flags only
  translatedArgs.emplace_back("-Wno-everything");
  translatedArgs.emplace_back("-fsyntax-only");

  clangCommand.CommandLine = std::move(translatedArgs);
  return clangCommand;
}

std::vector<clang::tooling::CompileCommand>
alchemy::parser::libclang::adapters::IARDbTranslator::translateAll(
    const clang::tooling::CompilationDatabase& db) const
{
  auto commands = db.getAllCompileCommands();
  std::vector<clang::tooling::CompileCommand> result;
  result.reserve(commands.size());

  for (const auto& cmd : commands)
  {
    result.emplace_back(translateCommand(cmd));
  }

  return result;
}

bool
alchemy::parser::libclang::adapters::IARDbTranslator::isIARCompiler(
    const clang::tooling::CompilationDatabase& db)
{
  auto allCommands = db.getAllCompileCommands();
  for (const auto& cmd : allCommands)
  {
    for (const auto& arg : cmd.CommandLine)
    {
      if (std::any_of(alchemy::parser::libclang::adapters::IARDbTranslator::
                          knownCompilerFlags()
                              .begin(),
                      alchemy::parser::libclang::adapters::IARDbTranslator::
                          knownCompilerFlags()
                              .end(),
                      [&arg](const std::string& flag) {
                        return arg.starts_with(flag);
                      }))
      {
        return true;
      }
    }
  }
  return false;
}

alchemy::core::Result<alchemy::parser::libclang::adapters::IARQueryConfig>
alchemy::parser::libclang::adapters::IARDbTranslator::extractQueryConfig(
    const clang::tooling::CompilationDatabase& db)
{
  auto commands = db.getAllCompileCommands();
  if (commands.empty())
  {
    return alchemy::core::
        Result<alchemy::parser::libclang::adapters::IARQueryConfig>::failure(
            alchemy::core::Error::format(
                "IARDbTranslator::extractQueryConfig",
                "compilation database contains no commands"));
  }
  // use first command in db to determine config
  const auto& cmd = commands[0];
  alchemy::parser::libclang::adapters::IARQueryConfig query;

  // extract compiler from cmd line
  const std::filesystem::path CompilerPath(cmd.CommandLine[0]);
  // preserve full path if provided, otherwise just filename (will be found via
  // PATH)
  query.compilerPath = CompilerPath.string();

  for (const auto& arg : cmd.CommandLine)
  {
    if (arg.starts_with("--cpu") || arg.starts_with("--fpu") ||
        arg.starts_with("--endian") || arg.starts_with("--dlib_config"))
    {
      query.archFlags.push_back(arg);
    }
  }

  return alchemy::core::
      Result<alchemy::parser::libclang::adapters::IARQueryConfig>::success(
          std::move(query));
}

alchemy::core::Result<std::string>
alchemy::parser::libclang::adapters::IARDbTranslator::executeCompiler(
    const std::string& compiler, const std::vector<std::string>& args)
{
  // build cmd string
  std::string cmd = compiler;
  for (const auto& arg : args)
  {
    cmd += " ";
    cmd += arg;
  }
  cmd += " 2>&1";  // redirect stderr to stdout

  // execute cmd and capture output
  std::array<char, 256> buffer{};
  std::string output;
  auto pipeDeleter = [](FILE* f) {
    if (f != nullptr)
    {
      pclose(f);
    };
  };
  // NOLINTNEXTLINE(cert-env33-c) - intentionally executing compiler from
  // compilation database
  std::unique_ptr<FILE, decltype(pipeDeleter)> pipe(
      popen(cmd.c_str(), "r"),  // NOLINT(cert-env33-c)
      pipeDeleter);

  if (!pipe)
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format("alchemy::parser::libclang::adapters::"
                                     "IARDbTranslator::executeCompiler",
                                     "failed to execute command: {}\n",
                                     cmd));
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
  {
    output += buffer.data();
  }

  int returnCode =
      pclose(pipe.release());  // release so unique_ptr doesn't close again
  if (returnCode != 0 && std::ranges::find(args, "--version") == args.end())
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format("alchemy::parser::libclang::adapters::"
                                     "IARDbTranslator::executeCompiler",
                                     "command failed with exit code {}: {}",
                                     returnCode,
                                     cmd));
  }

  return alchemy::core::Result<std::string>::success(std::move(output));
}

std::vector<std::string>
alchemy::parser::libclang::adapters::IARDbTranslator::parseSystemIncludes(
    const std::string& compilerOutput)
{
  std::unordered_set<std::filesystem::path> includes{};
  std::stringstream ss{compilerOutput};
  std::string line{};
  while (std::getline(ss, line))
  {
    // output pattern for paths: line 42 "/some/path..."
    // -> quote-delimited
    bool isPath = (line.find("line") != std::string::npos);
    auto startIdx = line.find_first_of('\"');
    auto endIdx = line.find_last_of('\"');
    if (isPath && startIdx != std::string::npos && endIdx != std::string::npos)
    {
      std::filesystem::path include{
          line.substr(startIdx + 1, endIdx - startIdx - 1)};
      includes.insert(include);
    }
  }
  std::vector<std::string> result{};
  result.reserve(includes.size());
  for (const auto& elem : includes)
  {
    result.emplace_back(elem.string());
  }
  return result;
}

std::vector<std::string>
alchemy::parser::libclang::adapters::IARDbTranslator::parseSystemDefines(
    const std::filesystem::path& predefFile)
{
  if (!std::filesystem::exists(predefFile))
  {
    fmt::print(
        stderr,
        "{}",
        alchemy::core::Error::format(
            "IARDbTranslator::parseSystemDefines",
            "predef file does not exist: {}\n"
            " check to make sure IAR compiler actually generated the file\n"
            "or check to see if there was an issue invoking the compiler",
            predefFile.string()));
    return {};
  }
  auto defines = std::vector<std::string>{};
  auto ifs = std::ifstream(predefFile, std::ios::in);
  if (!ifs.is_open())
  {
    fmt::print(
        stderr,
        "{}",
        alchemy::core::Error::format(
            "IARDbTranslator::parseSystemDefines",
            "predef file failed to open: {}\n"
            " check to make sure IAR compiler actually generated the file\n"
            "or check to see if there was an issue invoking the compiler",
            predefFile.string()));
    return {};
  }
  auto line = std::string{};
  while (std::getline(ifs, line))
  {
    // #define __ARM7EM__ 1 ==> -D__ARM7EM__=1
    if (line.find("#define") != std::string::npos)
    {
      auto keyBegin = std::size_t{line.find_first_of(' ') + 1};
      auto keyEnd = std::size_t{line.find_first_of(' ', keyBegin)};
      if (keyEnd == std::string::npos)
      {
        defines.emplace_back("-D" + line.substr(keyBegin));
      }
      else
      {
        auto key = std::string{line.substr(keyBegin, keyEnd - keyBegin)};
        auto value = std::string{line.substr(keyEnd + 1)};
        defines.emplace_back("-D" + key + "=" + value);
      }
    }
  }

  return defines;
}

alchemy::core::Result<std::vector<std::string>>
alchemy::parser::libclang::adapters::IARDbTranslator::querySystemIncludes(
    const alchemy::parser::libclang::adapters::IARQueryConfig& query)
{
  // stub out includes that should be preprocessed for clang-tool
  alchemy::core::filesystem::TempFile stubFile{"alchemy_intrinsics_stub", ".c"};
  std::ofstream ofs{stubFile.path, std::ios::out};
  ofs << "#include <intrinsics.h>\n";
  ofs.close();
  std::vector<std::string> queryArgs = {
      stubFile.path.string(), "--preprocess=l", "-"};

  // add arch flags from db
  queryArgs.insert(std::end(queryArgs),
                   std::begin(query.archFlags),
                   std::end(query.archFlags));

  // execute compiler for system paths/toolchain paths
  auto outputResult =
      alchemy::parser::libclang::adapters::IARDbTranslator::executeCompiler(
          query.compilerPath, queryArgs);
  if (outputResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IARDbTranslator::querySystemIncludes",
            "failed to execute {}: {}\n"
            " ensure IAR toolchain is installed and in PATH",
            query.compilerPath,
            outputResult.error()));
  }

  // parse includes from output
  auto includes =
      alchemy::parser::libclang::adapters::IARDbTranslator::parseSystemIncludes(
          outputResult.value());
  if (includes.empty())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IARDbTranslator::querySystemIncludes",
            "no include paths found in compiler output\n"
            " this may indicate an IAR installation issue\n"
            " try running manually: {} <stub_file_with_include> --preprocess=l "
            "-",
            query.compilerPath));
  }

  return alchemy::core::Result<std::vector<std::string>>::success(
      std::move(includes));
}

// derive Clang-compatible defines from IAR architecture flags
alchemy::core::Result<std::vector<std::string>>
alchemy::parser::libclang::adapters::IARDbTranslator::querySystemDefines(
    const alchemy::parser::libclang::adapters::IARQueryConfig& query)
{
  // build query cmd, e.g.
  // `iccarm stub.c --cpu=<archFlag> --predef_macros=n predef_macros.txt`
  alchemy::core::filesystem::TempFile stubFile{"alchemy_predef_macros_stub",
                                               ".c"};
  alchemy::core::filesystem::TempFile outFile{"alchemy_predef_macros", ".txt"};
  std::vector<std::string> queryArgs = {
      stubFile.path.string(),
      "--predef_macros=n",  // preprocess only, skip compilation
      outFile.path.string(),
  };

  // add arch flags from db
  queryArgs.insert(std::end(queryArgs),
                   std::begin(query.archFlags),
                   std::end(query.archFlags));

  // execute compiler for system paths/toolchain paths
  auto outputResult =
      alchemy::parser::libclang::adapters::IARDbTranslator::executeCompiler(
          query.compilerPath, queryArgs);
  if (outputResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IARDbTranslator::querySystemDefines",
            "failed to execute {}: {}\n"
            " ensure IAR toolchain is installed and in PATH",
            query.compilerPath,
            outputResult.error()));
  }

  std::vector<std::string> defines =
      alchemy::parser::libclang::adapters::IARDbTranslator::parseSystemDefines(
          std::filesystem::path(outFile.path));

  return alchemy::core::Result<std::vector<std::string>>::success(
      std::move(defines));
}

std::vector<std::string>
alchemy::parser::libclang::adapters::IARDbTranslator::
    getClangCompatibileDefines()
{
  return {// IAR intrinsic keywords - define as empty
          "-D__intrinsic=",
          "-D__noreturn=",
          "-D__root=",
          "-D__ramfunc=",
          "-D__no_init=",
          "-D__ro_placement=",
          // IAR attributes - map to Clang equivalents where possible
          "-D__packed=__attribute__((packed))",
          "-D__weak=__attribute__((weak))",
          "-D__inline=inline",
          "-D__always_inline=__attribute__((always_inline))",
          // static_assert compatibility (for nanopb and others)
          "-Dstatic_assert(...)=",
          // suppress warnings for unrecognized pragmas
          "-Wno-unknown-pragmas",
          "-Wno-ignored-attributes",
          "-Wno-unknown-attributes"};
}

const std::unordered_set<std::string>&
alchemy::parser::libclang::adapters::IARDbTranslator::knownCompilerFlags()
{
  static const std::unordered_set<std::string> CompilerFlags{
      // diagnostics
      "--diag_suppress",
      "--diag_warning",
      "--diag_error",
      "--diag_remark",
      // architecture/cpu
      "--cpu",
      "--fpu",
      "--endian",
      "--thumb",
      "--arm",
      "--interwork",
      // library configuration
      "--dlib_config",
      // optimization / code generation
      "--no_size_constraints",
      // IAR-specific features
      "--no_typedefs_in_diagnostics",
      "--no_path_in_file_macros",
      "--relaxed_fp",
      "--use_c++_inline",
      // memory model / embedded-specific
      "--data_model",
      "--code_model"};
  return CompilerFlags;
}
