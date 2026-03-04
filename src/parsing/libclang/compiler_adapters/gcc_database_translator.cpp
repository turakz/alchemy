// src/parsing/libclang/compiler_adapters/gcc_database_translator.cpp
#include "parsing/libclang/compiler_adapters/gcc_database_translator.hpp"

// std
#include <cstddef>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>
#include <fmt/core.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"

alchemy::parser::libclang::adapters::GccDbTranslator::GccDbTranslator(
    const std::vector<std::string>& sysIncludes,
    const std::string& targetTriple)
  : m_sysIncludes(sysIncludes), m_targetTriple(targetTriple)
{
}

clang::tooling::CompileCommand
alchemy::parser::libclang::adapters::GccDbTranslator::translateCommand(
    const clang::tooling::CompileCommand& gccCommand) const
{
  clang::tooling::CompileCommand clangCommand = gccCommand;
  std::vector<std::string> translatedArgs;

  translatedArgs.emplace_back("clang");

  // inject target triple (queried from compiler via -dumpmachine)
  if (!m_targetTriple.empty())
  {
    translatedArgs.emplace_back("-target");
    translatedArgs.emplace_back(m_targetTriple);
  }

  // ARM EABI targets default to short (packed) enums — GCC's arm-none-eabi
  // enables -fshort-enums implicitly. clang does not mirror this default,
  // so inject it explicitly to match GCC's ABI for correct sizeof() results.
  if (isArmEabiTarget(m_targetTriple))
  {
    translatedArgs.emplace_back("-fshort-enums");
  }

  // inject queried system includes
  for (const auto& path : m_sysIncludes)
  {
    translatedArgs.emplace_back("-isystem");
    translatedArgs.emplace_back(path);
  }

  // flags that suppress clang's resource directory — stripping these ensures
  // clang can resolve standard types (stdint.h, etc.) from its target-correct
  // resource dir headers.
  // note: -nostdinc/-nostdinc++ are shared between gcc and clang;
  // -nobuiltininc/-nostdlibinc are clang-specific but may appear in build
  // systems that generate commands for multiple compilers
  static const std::unordered_set<std::string> ResourceDirSuppressors = {
      "-nostdinc",
      "-nostdinc++",
      "-nobuiltininc",
      "-nostdlibinc",
  };

  // translate gcc flags to clang-compatible flags
  // note: architecture flags (-mcpu, -march, -mthumb, -marm, -mfloat-abi,
  // -mfpu, -mabi, -mtune) are intentionally preserved — clang understands
  // them and they are essential for correct type layout and alignment
  for (std::size_t idx = 1; idx < gccCommand.CommandLine.size(); ++idx)
  {
    const auto& arg = gccCommand.CommandLine[idx];

    // strip --driver-mode= injected by inferTargetAndDriverMode — alchemy
    // handles flag translation directly, GCC driver mode is not needed
    if (arg.starts_with("--driver-mode="))
    {
      continue;
    }

    // strip known GCC-only flags, flags that suppress clang's resource
    // directory, and -Werror (not needed for AST parsing)
    if (gccFlags().contains(arg) || isGccFlagPrefix(arg) ||
        ResourceDirSuppressors.contains(arg))
    {
      continue;
    }

    auto itr = gccToClangTranslation().find(arg);
    if (itr != std::end(gccToClangTranslation()))
    {
      translatedArgs.emplace_back(itr->second);
    }
    else
    {
      // keep everything else (-I, -D, -O, -W shared flags, source files, etc.)
      translatedArgs.emplace_back(arg);
    }
  }

  // safety net: suppress unknown warning errors so any GCC warnings we missed
  // don't cause hard failures when -Werror is present in the original commands
  translatedArgs.emplace_back("-Wno-unknown-warning-option");
  translatedArgs.emplace_back("-Wno-ignored-attributes");
  translatedArgs.emplace_back("-Wno-typedef-redefinition");

  // alchemy only needs AST parsing, not code generation
  translatedArgs.emplace_back("-fsyntax-only");

  clangCommand.CommandLine = std::move(translatedArgs);
  return clangCommand;
}

bool
alchemy::parser::libclang::adapters::GccDbTranslator::isGccCompiler(
    const clang::tooling::CompilationDatabase& db)
{
  auto allCommands = db.getAllCompileCommands();
  for (const auto& cmd : allCommands)
  {
    if (cmd.CommandLine.empty())
    {
      continue;
    }

    // check compiler binary name (CommandLine[0])
    auto compilerPath = std::filesystem::path(cmd.CommandLine[0]);
    auto compilerName = compilerPath.filename().string();

    // reject clang/clang++ — those are native clang databases
    if (compilerName.starts_with("clang"))
    {
      return false;
    }

    // match gcc, g++, cc, c++ (including cross-compile prefixes like
    // arm-none-eabi-gcc)
    if (compilerName == "gcc" || compilerName == "g++" ||
        compilerName == "cc" || compilerName == "c++" ||
        compilerName.ends_with("-gcc") || compilerName.ends_with("-g++"))
    {
      return true;
    }

    // fallback: check for GCC-only flags in arguments
    for (const auto& arg : cmd.CommandLine)
    {
      if ((gccFlags().find(arg) != std::end(gccFlags())) ||
          isGccFlagPrefix(arg))
      {
        return true;
      }
    }
  }

  return false;
}

alchemy::core::Result<alchemy::parser::libclang::adapters::GccQueryConfig>
alchemy::parser::libclang::adapters::GccDbTranslator::extractQueryConfig(
    const clang::tooling::CompilationDatabase& db)
{
  auto commands = db.getAllCompileCommands();
  if (commands.empty())
  {
    return alchemy::core::
        Result<alchemy::parser::libclang::adapters::GccQueryConfig>::failure(
            alchemy::core::Error::format(
                "GccDbTranslator::extractQueryConfig",
                "compilation database contains no commands"));
  }

  const auto& cmd = commands[0];
  alchemy::parser::libclang::adapters::GccQueryConfig query;

  // extract compiler path from first command
  const std::filesystem::path CompilerPath(cmd.CommandLine[0]);
  query.compilerPath = CompilerPath.string();

  // infer language from compiler binary name
  auto compilerName = CompilerPath.filename().string();
  if (compilerName == "g++" || compilerName == "c++" ||
      compilerName.ends_with("-g++"))
  {
    query.language = "c++";
  }
  else
  {
    query.language = "c";
  }

  return alchemy::core::
      Result<alchemy::parser::libclang::adapters::GccQueryConfig>::success(
          std::move(query));
}

std::vector<std::string>
alchemy::parser::libclang::adapters::GccDbTranslator::parseSystemIncludes(
    const std::string& compilerOutput)
{
  std::vector<std::string> includes;
  std::istringstream ss(compilerOutput);
  std::string line;
  bool inSearchList = false;

  while (std::getline(ss, line))
  {
    if (line.find("#include <...> search starts here:") != std::string::npos)
    {
      inSearchList = true;
      continue;
    }
    if (line.find("End of search list.") != std::string::npos)
    {
      break;
    }
    if (inSearchList && !line.empty() && line[0] == ' ')
    {
      // trim leading whitespace
      auto start = line.find_first_not_of(' ');
      if (start != std::string::npos)
      {
        auto path = line.substr(start);
        // strip " (framework directory)" suffix if present
        auto suffix = path.find(" (framework directory)");
        if (suffix != std::string::npos)
        {
          path = path.substr(0, suffix);
        }
        includes.emplace_back(std::move(path));
      }
    }
  }

  return includes;
}

alchemy::core::Result<std::vector<std::string>>
alchemy::parser::libclang::adapters::GccDbTranslator::querySystemIncludes(
    const GccQueryConfig& query)
{
  // gcc -E -Wp,-v -x c /dev/null 2>&1
  // include search paths are printed to stderr, captured via 2>&1
  const std::vector<std::string> Args = {
      "-E", "-Wp,-v", "-x", query.language, "/dev/null"};

  auto outputResult =
      alchemy::parser::libclang::adapters::executeCompilerCommand(
          query.compilerPath, Args, "GccDbTranslator::querySystemIncludes");
  if (outputResult.invalid())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format("GccDbTranslator::querySystemIncludes",
                                     "failed to execute {}: {}",
                                     query.compilerPath,
                                     outputResult.error()));
  }

  auto includes = parseSystemIncludes(outputResult.value());
  if (includes.empty())
  {
    return alchemy::core::Result<std::vector<std::string>>::failure(
        alchemy::core::Error::format(
            "GccDbTranslator::querySystemIncludes",
            "no include paths found in compiler output"));
  }

  return alchemy::core::Result<std::vector<std::string>>::success(
      std::move(includes));
}

std::string
alchemy::parser::libclang::adapters::GccDbTranslator::validateTargetTriple(
    const std::string& gccTriple)
{
  // extract architecture component (first element before first '-')
  auto dashPos = gccTriple.find('-');
  std::string arch =
      (dashPos != std::string::npos) ? gccTriple.substr(0, dashPos) : gccTriple;

  // architectures that clang is documented to support as target triples
  // clang accepts raw GCC triples for these — combined with architecture
  // flags (-mcpu, -march, -mfloat-abi, etc.) that pass through the
  // translator, clang resolves correct type layout and alignment
  static const std::unordered_set<std::string> KnownArchitectures = {
      // ARM (baremetal embedded targets)
      "arm",
      "armv6m",
      "armv7a",
      "armv7m",
      "armv7em",
      "armv7r",
      "armv8a",
      "armv8m",
      "thumb",
      "thumbv6m",
      "thumbv7m",
      "thumbv7em",
      "thumbv8m",
      // AArch64
      "aarch64",
      // x86 (dev host targets)
      "i386",
      "i686",
      "x86_64",
      // RISC-V
      "riscv32",
      "riscv64",
      // AVR (Arduino/embedded)
      "avr",
      // MSP430 (TI embedded)
      "msp430",
  };

  if (!KnownArchitectures.contains(arch))
  {
    fmt::print(stderr,
               "alchemy::{}parser{}::{}warning{}: GCC target triple '{}' has "
               "unrecognized architecture '{}'\n"
               "  clang may fall back to host target, which would produce "
               "incorrect struct alignment results\n"
               "  known architectures: arm, aarch64, x86_64, i686, riscv32, "
               "riscv64, avr, msp430, ...\n",
               alchemy::color::ansi::BoldBrightGreen,
               alchemy::color::ansi::Reset,
               alchemy::color::ansi::Yellow,
               alchemy::color::ansi::Reset,
               gccTriple,
               arch);
  }

  return gccTriple;
}

alchemy::core::Result<std::string>
alchemy::parser::libclang::adapters::GccDbTranslator::queryTargetTriple(
    const GccQueryConfig& query)
{
  auto result = alchemy::parser::libclang::adapters::executeCompilerCommand(
      query.compilerPath,
      {"-dumpmachine"},
      "GccDbTranslator::queryTargetTriple");
  if (result.invalid())
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format("GccDbTranslator::queryTargetTriple",
                                     "failed to query {}: {}",
                                     query.compilerPath,
                                     result.error()));
  }

  auto triple = result.value();
  while (!triple.empty() && (triple.back() == '\n' || triple.back() == '\r'))
  {
    triple.pop_back();
  }

  if (triple.empty())
  {
    return alchemy::core::Result<std::string>::failure(
        alchemy::core::Error::format("GccDbTranslator::queryTargetTriple",
                                     "compiler returned empty target triple"));
  }

  auto validated = alchemy::parser::libclang::adapters::GccDbTranslator::
      validateTargetTriple(triple);
  return alchemy::core::Result<std::string>::success(std::move(validated));
}

const std::unordered_set<std::string>&
alchemy::parser::libclang::adapters::GccDbTranslator::gccFlags()
{
  static const std::unordered_set<std::string> GccFlags = {
      // gcc codegen flags
      "-fcallgraph-info",
      "-fstack-usage",
      // gcc debug flags
      "-fvar-tracking-assignments",

      // gcc warning flags
      "-Wformat-signedness",
      "-Wno-stringop-overread",
      "-Wno-stringop-overflow",
      "-Wno-builtin-declaration-mismatch",
      "-Wstringop-truncation",
      "-Wno-stringop-truncation",
      "-Wno-array-parameter",
      "-Wno-restrict",
      "-Wno-alloca",
      "-Wlogical-op",
      "-Wduplicated-branches",
      "-Werror",

      // gcc linker spec flags (not relevant for AST parsing)
      "--specs=nano.specs",
      "--specs=nosys.specs",
      "--specs=rdimon.specs"};
  return GccFlags;
}

bool
alchemy::parser::libclang::adapters::GccDbTranslator::isGccFlagPrefix(
    const std::string& arg)
{
  static const std::vector<std::string> Prefixes = {
      "-fcallgraph-info=",
      "-fdump-",
      "-fipa-",
      "-Wsuggest-attribute=",
      "-Werror=suggest-attribute=",
      "-Werror="};

  return std::any_of(
      std::begin(Prefixes),
      std::end(Prefixes),
      [&arg](const std::string& prefix) { return arg.starts_with(prefix); });
}

bool
alchemy::parser::libclang::adapters::GccDbTranslator::isArmEabiTarget(
    const std::string& triple)
{
  // matches arm-none-eabi, arm-none-eabihf, thumbv7em-none-eabi, etc.
  // does NOT match arm-linux-gnueabihf (Linux ARM does not use short enums)
  return (triple.starts_with("arm") || triple.starts_with("thumb")) &&
         triple.find("-none-eabi") != std::string::npos;
}

const std::unordered_map<std::string, std::string>&
alchemy::parser::libclang::adapters::GccDbTranslator::gccToClangTranslation()
{
  static const std::unordered_map<std::string, std::string> TranslatedFlags{
      {"-Wmaybe-uninitialized", "-Wconditional-uninitialized"},
      {"-Wno-maybe-uninitialized", "-Wno-conditional-uninitialized"},
      {"-Wvolatile", "-Wdeprecated-volatile"},
      {"-Wno-volatile", "-Wno-deprecated-volatile"},
      {"-Wdiscarded-qualifiers",
       "-Wincompatible-pointer-types-discards-qualifiers"},
      {"-Wno-discarded-qualifiers",
       "-Wno-incompatible-pointer-types-discards-qualifiers"},
  };
  return TranslatedFlags;
}
