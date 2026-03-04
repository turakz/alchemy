// src/parsing/libclang/compiler_adapters/iar_database_translator.cpp
#include "parsing/libclang/compiler_adapters/iar_database_translator.hpp"

// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

// local
#include "app/core/core.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"

clang::tooling::CompileCommand
alchemy::parser::libclang::adapters::IarDbTranslator::translateCommand(
    const clang::tooling::CompileCommand& iarCommand) const
{
  clang::tooling::CompileCommand clangCommand = iarCommand;
  std::vector<std::string> translatedArgs;
  translatedArgs.emplace_back("clang");

  // provide clang system headers so it can resolve
  // std includes found in project headers when forming an AST
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
  auto compatDefines = alchemy::parser::libclang::adapters::IarDbTranslator::
      getClangCompatibleDefines();
  translatedArgs.insert(std::end(translatedArgs),
                        std::begin(compatDefines),
                        std::end(compatDefines));

  // allowlist: only keep flags that clang understands
  // IAR's flag syntax is completely different from clang's — there is almost
  // no overlap beyond -D/-I/-U, so an allowlist is safer than a blocklist
  for (std::size_t idx = 1; idx < iarCommand.CommandLine.size(); ++idx)
  {
    const auto& arg = iarCommand.CommandLine[idx];

    // preprocessor defines, include paths, and undefines
    if (arg.starts_with("-D") || arg.starts_with("-I") || arg.starts_with("-U"))
    {
      translatedArgs.emplace_back(arg);
      continue;
    }

    // skip -o and its argument (output file path)
    if (arg == "-o")
    {
      ++idx;  // skip the output path that follows
      continue;
    }

    // skip all other flags (IAR-specific)
    // if a -- flag is followed by a non-flag token, skip that too
    // (it's a space-separated argument, e.g. --dlib_config normal)
    if (arg.starts_with("--"))
    {
      if (idx + 1 < iarCommand.CommandLine.size() &&
          !iarCommand.CommandLine[idx + 1].starts_with("-"))
      {
        ++idx;
      }
      continue;
    }
    if (arg.starts_with("-"))
    {
      continue;
    }

    // non-flag arguments are source file paths
    translatedArgs.emplace_back(arg);
  }

  // TODO(ENGPROD-747): hardcoded to ARM because alchemy only detects iccarm
  // today
  // -> if we add support for other IAR compilers (iccavr, iccrx, iccrl78,
  // etc.), detect the target by parsing the compiler binary name, the version
  // banner ("IAR C/C++ Compiler ... for ARM"), or predefined macros
  // (__ICCARM__, __ICCAVR__, etc.) and map to the appropriate clang target
  // triple
  translatedArgs.emplace_back("-target");
  translatedArgs.emplace_back("arm-none-eabi");
  // ARM EABI defaults to short (packed) enums — inject explicitly for clang
  translatedArgs.emplace_back("-fshort-enums");
  // parsing flags only
  translatedArgs.emplace_back("-Wno-everything");
  translatedArgs.emplace_back("-fsyntax-only");

  clangCommand.CommandLine = std::move(translatedArgs);
  return clangCommand;
}

bool
alchemy::parser::libclang::adapters::IarDbTranslator::isIarCompiler(
    const clang::tooling::CompilationDatabase& db)
{
  auto allCommands = db.getAllCompileCommands();
  for (const auto& cmd : allCommands)
  {
    for (const auto& arg : cmd.CommandLine)
    {
      if (std::any_of(std::begin(alchemy::parser::libclang::adapters::
                                     IarDbTranslator::knownCompilerFlags()),
                      std::end(alchemy::parser::libclang::adapters::
                                   IarDbTranslator::knownCompilerFlags()),
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

alchemy::core::Result<alchemy::parser::libclang::adapters::IarQueryConfig>
alchemy::parser::libclang::adapters::IarDbTranslator::extractQueryConfig(
    const clang::tooling::CompilationDatabase& db)
{
  auto commands = db.getAllCompileCommands();
  if (commands.empty())
  {
    return alchemy::core::
        Result<alchemy::parser::libclang::adapters::IarQueryConfig>::failure(
            alchemy::core::Error::format(
                "IarDbTranslator::extractQueryConfig",
                "compilation database contains no commands"));
  }
  // use first command in db to determine config
  const auto& cmd = commands[0];
  alchemy::parser::libclang::adapters::IarQueryConfig query;

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
      Result<alchemy::parser::libclang::adapters::IarQueryConfig>::success(
          std::move(query));
}

std::vector<std::string>
alchemy::parser::libclang::adapters::IarDbTranslator::parseSystemIncludes(
    const std::string& compilerOutput)
{
  std::unordered_set<std::filesystem::path> includes{};
  std::stringstream ss{compilerOutput};
  std::string line{};
  while (std::getline(ss, line))
  {
    // output pattern for paths: line 42 "/some/path..."
    // -> quote-delimited
    const bool IsPath = (line.find("line") != std::string::npos);
    auto startIdx = line.find_first_of('\"');
    auto endIdx = line.find_last_of('\"');
    if (IsPath && startIdx != std::string::npos && endIdx != std::string::npos)
    {
      const std::filesystem::path Include{
          line.substr(startIdx + 1, endIdx - startIdx - 1)};
      includes.insert(Include);
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

alchemy::core::Result<std::vector<std::string>>
alchemy::parser::libclang::adapters::IarDbTranslator::parseSystemDefines(
    const std::filesystem::path& predefFile)
{
  if (!std::filesystem::exists(predefFile))
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IarDbTranslator::parseSystemDefines",
            "predef file does not exist: {}\n"
            " check to make sure IAR compiler actually generated the file\n"
            "or check to see if there was an issue invoking the compiler",
            predefFile.string()));
  }
  auto defines = std::vector<std::string>{};
  auto ifs = std::ifstream(predefFile, std::ios::in);
  if (!ifs.is_open())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IarDbTranslator::parseSystemDefines",
            "predef file failed to open: {}\n"
            " check to make sure IAR compiler actually generated the file\n"
            "or check to see if there was an issue invoking the compiler",
            predefFile.string()));
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
        std::string define = "-D";
        define += key;
        define += "=";
        define += value;
        defines.emplace_back(std::move(define));
      }
    }
  }

  return alchemy::core::Result<std::vector<std::string>>::success(
      std::move(defines));
}

alchemy::core::Result<std::vector<std::string>>
alchemy::parser::libclang::adapters::IarDbTranslator::querySystemIncludes(
    const alchemy::parser::libclang::adapters::IarQueryConfig& query)
{
  // stub out includes that should be preprocessed for clang-tool
  const alchemy::core::filesystem::TempFile StubFile{"alchemy_intrinsics_stub",
                                                     ".c"};
  std::ofstream ofs{StubFile.path, std::ios::out};
  ofs << "#include <intrinsics.h>\n";
  ofs.close();
  std::vector<std::string> queryArgs = {
      StubFile.path.string(), "--preprocess=l", "-"};

  // add arch flags from db
  queryArgs.insert(std::end(queryArgs),
                   std::begin(query.archFlags),
                   std::end(query.archFlags));

  // execute compiler for system paths/toolchain paths
  auto outputResult =
      alchemy::parser::libclang::adapters::executeCompilerCommand(
          query.compilerPath,
          queryArgs,
          "IarDbTranslator::querySystemIncludes");
  if (outputResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IarDbTranslator::querySystemIncludes",
            "failed to execute {}: {}\n"
            " ensure IAR toolchain is installed and in PATH",
            query.compilerPath,
            outputResult.error()));
  }

  // parse includes from output
  auto includes =
      alchemy::parser::libclang::adapters::IarDbTranslator::parseSystemIncludes(
          outputResult.value());
  if (includes.empty())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IarDbTranslator::querySystemIncludes",
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
alchemy::parser::libclang::adapters::IarDbTranslator::querySystemDefines(
    const alchemy::parser::libclang::adapters::IarQueryConfig& query)
{
  // build query cmd, e.g.
  // `iccarm stub.c --cpu=<archFlag> --predef_macros=n predef_macros.txt`
  const alchemy::core::filesystem::TempFile StubFile{
      "alchemy_predef_macros_stub", ".c"};
  const alchemy::core::filesystem::TempFile OutFile{"alchemy_predef_macros",
                                                    ".txt"};
  std::vector<std::string> queryArgs = {
      StubFile.path.string(),
      "--predef_macros=n",  // preprocess only, skip compilation
      OutFile.path.string(),
  };

  // add arch flags from db
  queryArgs.insert(std::end(queryArgs),
                   std::begin(query.archFlags),
                   std::end(query.archFlags));

  // execute compiler for system paths/toolchain paths
  auto outputResult =
      alchemy::parser::libclang::adapters::executeCompilerCommand(
          query.compilerPath, queryArgs, "IarDbTranslator::querySystemDefines");
  if (outputResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "IarDbTranslator::querySystemDefines",
            "failed to execute {}: {}\n"
            " ensure IAR toolchain is installed and in PATH",
            query.compilerPath,
            outputResult.error()));
  }

  auto definesResult =
      alchemy::parser::libclang::adapters::IarDbTranslator::parseSystemDefines(
          std::filesystem::path(OutFile.path));
  if (definesResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        std::move(definesResult).error());
  }

  return alchemy::core::Result<std::vector<std::string>>::success(
      std::move(definesResult).value());
}

std::vector<std::string>
alchemy::parser::libclang::adapters::IarDbTranslator::
    getClangCompatibleDefines()
{
  return {// IAR intrinsic keywords — define as empty (no clang equivalent)
          "-D__intrinsic=",
          "-D__noreturn=",
          "-D__root=",
          "-D__ramfunc=",
          "-D__no_init=",
          "-D__ro_placement=",
          "-D__nounwind=",
          "-D__nested=",
          "-D__task=",
          "-D__swi=",
          "-D__irq=",
          "-D__fiq=",
          "-D__stackless=",
          // IAR mode/endian qualifiers — define as empty
          "-D__arm=",
          "-D__thumb=",
          "-D__big_endian=",
          "-D__little_endian=",
          "-D__interwork=",
          // IAR attributes — map to clang equivalents where possible
          "-D__packed=__attribute__((packed))",
          "-D__weak=__attribute__((weak))",
          "-D__inline=inline",
          "-D__always_inline=__attribute__((always_inline))",
          "-D__noinline=__attribute__((noinline))",
          // static_assert compatibility (for nanopb and others)
          "-Dstatic_assert(...)=",
          // suppress warnings for unrecognized pragmas/attributes
          "-Wno-unknown-pragmas",
          "-Wno-ignored-attributes",
          "-Wno-unknown-attributes"};
}

const std::unordered_set<std::string>&
alchemy::parser::libclang::adapters::IarDbTranslator::knownCompilerFlags()
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
