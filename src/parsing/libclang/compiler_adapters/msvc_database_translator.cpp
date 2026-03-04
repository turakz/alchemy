// src/parsing/libclang/compiler_adapters/msvc_database_translator.cpp
#include "parsing/libclang/compiler_adapters/msvc_database_translator.hpp"

// std
#include <cstddef>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/Tooling/CompilationDatabase.h>

clang::tooling::CompileCommand
alchemy::parser::libclang::adapters::MsvcDbTranslator::translateCommand(
    const clang::tooling::CompileCommand& msvcCommand) const
{
  clang::tooling::CompileCommand clangCommand = msvcCommand;
  std::vector<std::string> translatedArgs;

  translatedArgs.emplace_back("clang");

  // msvc compatibility
  translatedArgs.emplace_back("-fms-extensions");
  translatedArgs.emplace_back("-fms-compatibility");
  // TODO(ENGPROD-747): hardcoded to x86_64 Windows. MSVC uses separate cl.exe
  // binaries per target (x86, x64, ARM, ARM64)
  // -> detect the target by parsing the cl.exe path (e.g.,
  // Hostx64/arm64/cl.exe), the banner output ("Microsoft C/C++ Compiler ... for
  // x64"), or predefined macros
  // (_M_IX86, _M_X64, _M_ARM, _M_ARM64) and map to the appropriate clang target
  // triple
  translatedArgs.emplace_back("-target");
  translatedArgs.emplace_back("x86_64-pc-windows-msvc");

  // translate msvc flags to clang
  for (std::size_t idx = 1; idx < msvcCommand.CommandLine.size(); ++idx)
  {
    const auto& arg = msvcCommand.CommandLine[idx];
    if (alchemy::parser::libclang::adapters::MsvcDbTranslator::
            knownCompilerFlags()
                .contains(arg))
    {
      continue;
    }
    if (arg.starts_with("/I"))
    {
      // /Iinclude → -I include
      if (arg.length() > 2)
      {
        translatedArgs.emplace_back("-I");
        translatedArgs.emplace_back(arg.substr(2));  // remove /I prefix
      }
      else if (idx + 1 < msvcCommand.CommandLine.size())
      {
        // /I include (space-separated)
        translatedArgs.emplace_back("-I");
        translatedArgs.emplace_back(msvcCommand.CommandLine[++idx]);
      }
      // else: malformed /I at end of command line, skip
    }
    else if (arg.starts_with("/D"))
    {
      // /DDEBUG → -D DEBUG
      if (arg.length() > 2)
      {
        translatedArgs.emplace_back("-D");
        translatedArgs.emplace_back(arg.substr(2));
      }
      else if (idx + 1 < msvcCommand.CommandLine.size())
      {
        translatedArgs.emplace_back("-D");
        translatedArgs.emplace_back(msvcCommand.CommandLine[++idx]);
      }
      // else: malformed /D at end of command line, skip
    }
    else if (arg.starts_with("/U"))
    {
      // /UFEATURE → -U FEATURE
      if (arg.length() > 2)
      {
        translatedArgs.emplace_back("-U");
        translatedArgs.emplace_back(arg.substr(2));
      }
      else if (idx + 1 < msvcCommand.CommandLine.size())
      {
        translatedArgs.emplace_back("-U");
        translatedArgs.emplace_back(msvcCommand.CommandLine[++idx]);
      }
      // else: malformed /U at end of command line, skip
    }
    else if (arg.starts_with("/std:"))
    {
      // /std:c++17 → -std=c++17
      translatedArgs.emplace_back("-std=" + arg.substr(5));
    }
    else if (arg == "/TC")
    {
      // /TC → -x c
      translatedArgs.emplace_back("-x");
      translatedArgs.emplace_back("c");
    }
    else if (arg == "/TP")
    {
      // /TP → -x c++
      translatedArgs.emplace_back("-x");
      translatedArgs.emplace_back("c++");
    }
    else
    {
      // keep everything else (source files, etc.)
      translatedArgs.emplace_back(arg);
    }
  }

  // parsing-only flags
  translatedArgs.emplace_back("-Wno-everything");
  translatedArgs.emplace_back("-fsyntax-only");

  clangCommand.CommandLine = std::move(translatedArgs);
  return clangCommand;
}

bool
alchemy::parser::libclang::adapters::MsvcDbTranslator::isMsvcCompiler(
    const clang::tooling::CompilationDatabase& db)
{
  auto allCommands = db.getAllCompileCommands();
  // check for msvc specific indicators
  for (const auto& cmd : allCommands)
  {
    for (const auto& arg : cmd.CommandLine)
    {
      if (alchemy::parser::libclang::adapters::MsvcDbTranslator::
              knownCompilerFlags()
                  .contains(arg))
      {
        return true;
      }
    }
  }
  // if no commands, not MSVC
  return false;
}

const std::unordered_set<std::string>&
alchemy::parser::libclang::adapters::MsvcDbTranslator::knownCompilerFlags()
{
  static const std::unordered_set<std::string> CompilerFlags = {
      // runtime library linking (MSVC-specific)
      "/MD",
      "/MDd",
      "/MT",
      "/MTd",
      "/LD",
      "/LDd",
      // MSVC-specific optimization
      "/O1",
      "/O2",
      "/Ox",
      "/Oy",
      "/GL",
      // exception handling (MSVC-specific syntax)
      "/EHa",
      "/EHs",
      "/EHc",
      "/EHsc",
      // security/runtime checks (MSVC-specific)
      "/GS",
      "/RTC1",
      "/RTCc",
      "/RTCs",
      "/RTCu",
      "/sdl",
      // calling conventions (x86 Windows-specific)
      "/Gd",
      "/Gr",
      "/Gv",
      "/Gz",
      // preprocessing (MSVC syntax)
      "/E",
      "/P",
      "/EP",
      // debug info (MSVC-specific)
      "/Z7",
      "/Zi",
      "/ZI",
      // precompiled headers (MSVC-specific)
      "/Yc",
      "/Yu",
      "/Fp",
      // warnings (MSVC syntax: /W vs -W)
      "/W0",
      "/W1",
      "/W2",
      "/W3",
      "/W4",
      "/Wall",
      "/WX",
      "/wd",
      "/we",
      "/wo",
      "/w",  // + number variants
      // c++ conformance (MSVC-specific)
      "/permissive-",
      "/Zc:inline",
      "/Zc:preprocessor",
      "/Zc:wchar_t",
      "/Zc:forScope",
      "/Zc:auto",
      // security mitigations (Windows-specific)
      "/Qspectre",
      "/guard:cf",
      "/guard:ehcont",
      // performance/parallelization (MSVC-specific)
      "/Qpar",
      "/openmp",
      "/MP",
      // windows-specific code gen
      "/kernel",
      "/hotpatch",
      "/vd0",
      "/vd1",
      "/vd2",
      "/vmb",
      "/vmg",
      // static analysis (MSVC-specific)
      "/analyze",
      // basic compilation flags (MSVC syntax with /)
      "/c",   // compile only (vs -c for GCC/Clang/IAR)
      "/Fo",  // output object file (vs -o for GCC/Clang/IAR)
      "/TC",  // treat as C source
      "/TP",  // treat as C++ source
  };
  return CompilerFlags;
}
