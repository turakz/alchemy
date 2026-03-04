// tests/data/mock_compilers/mock_iccarm.cpp
// mock IAR compiler for unit testing
// handles: --preprocess=l (include query) and --predef_macros=n (defines query)
#include <cstdint>

#include <fstream>
#include <string>

#include "fmt/core.h"

std::int32_t
main(int argc, char* argv[])
{
  std::string outputFile;
  std::string cpuArch;
  bool predefMacros = false;
  bool preprocess = false;

  // parse arguments
  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--predef_macros=n" && i + 1 < argc)
    {
      predefMacros = true;
      outputFile = argv[++i];
    }
    else if (arg.starts_with("--cpu="))
    {
      cpuArch = arg.substr(6);
    }
    else if (arg == "--preprocess=l")
    {
      preprocess = true;
    }
  }

  // handle --predef_macros=n <output_file>
  // writes architecture-specific defines to output file
  if (predefMacros && !outputFile.empty())
  {
    std::ofstream ofs(outputFile);
    if (!ofs.is_open())
    {
      fmt::print(
          stderr, "mock_iccarm: failed to open output file: {}\n", outputFile);
      return 1;
    }

    // write architecture-specific defines based on --cpu flag
    // order matters: M33 before M3, M0+ before M0
    if (cpuArch.find("M0+") != std::string::npos ||
        cpuArch.find("M0plus") != std::string::npos)
    {
      ofs << "#define __ARM6M__ 1\n";
      ofs << "#define __CORE__ __ARM6M__\n";
    }
    else if (cpuArch.find("M0") != std::string::npos)
    {
      ofs << "#define __ARM6M__ 1\n";
      ofs << "#define __CORE__ __ARM6M__\n";
    }
    else if (cpuArch.find("M33") != std::string::npos)
    {
      ofs << "#define __ARM8M_MAINLINE__ 1\n";
      ofs << "#define __CORE__ __ARM8M_MAINLINE__\n";
    }
    else if (cpuArch.find("M23") != std::string::npos)
    {
      ofs << "#define __ARM8M_BASELINE__ 1\n";
      ofs << "#define __CORE__ __ARM8M_BASELINE__\n";
    }
    else if (cpuArch.find("M4") != std::string::npos ||
             cpuArch.find("M7") != std::string::npos)
    {
      ofs << "#define __ARM7EM__ 1\n";
      ofs << "#define __CORE__ __ARM7EM__\n";
    }
    else if (cpuArch.find("M3") != std::string::npos)
    {
      ofs << "#define __ARM7M__ 1\n";
      ofs << "#define __CORE__ __ARM7M__\n";
    }
    else
    {
      // fallback to M4/ARMv7E-M (most common)
      ofs << "#define __ARM7EM__ 1\n";
      ofs << "#define __CORE__ __ARM7EM__\n";
    }

    return 0;
  }

  // handle --preprocess=l (or default)
  // outputs preprocessed source with #line markers containing include paths
  if (preprocess || argc > 1)
  {
    fmt::print("#line 1 \"/opt/iar/arm/inc/c/intrinsics.h\"\n");
    fmt::print("#line 24 \"/opt/iar/arm/inc/c/stdint.h\"\n");
    fmt::print("#line 8 \"/opt/iar/arm/inc/c/aarch32/stddef.h\"\n");
    fmt::print("#line 15 \"/opt/iar/arm/CMSIS/Core/Include/core_cm4.h\"\n");
    fmt::print("#line 42 \"/opt/iar/arm/CMSIS/DSP/Include/arm_math.h\"\n");
    return 0;
  }

  // no arguments - print version info
  fmt::print("IAR ANSI C/C++ Compiler V8.50.6.265/W32 for ARM\n");
  fmt::print("Copyright 1999-2020 IAR Systems AB.\n");

  return 0;
}
