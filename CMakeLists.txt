# ============================================================================
# Alchemy CMake Configuration
# ============================================================================
# minimum CMake version: 3.21 for CMakePresets.json build/test presets
# (previously 3.14 for FetchContent; bumped for modern preset support)
cmake_minimum_required(VERSION 3.21)

# project declaration
project(alchemy
  LANGUAGES C CXX
  VERSION 0.1.0
  DESCRIPTION "c++ refactoring and code generation tool built on libclang/llvm"
)

set(ALCHEMY_VERSION_SUFFIX "alpha")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/inc/app/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/app/version.hpp
  @ONLY
)

# ============================================================================
# build configuration
# ============================================================================

# C++ standard: C++20 required for designated initializers, concepts, ranges, etc.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # disable GNU extensions (-std=c++20, not -std=gnu++20)

# colored compiler diagnostics (applied to all targets)
add_compile_options(-fdiagnostics-color=always)

# export compile_commands.json for tooling (clang-tidy, clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# toolchain validation
# ============================================================================

# verify that our clang toolchain was loaded
# this catches cases where user forgot -DCMAKE_TOOLCHAIN_FILE=...
if(NOT DEFINED ALCHEMY_TOOLCHAIN_LOADED)
  message(WARNING
    "\nalchemy::WARNING: clang toolchain not detected!\n"
    "-> this may cause inconsistent builds across different machines.\n"
    "-> recommend running CMake with presets:\n"
    "   cmake --preset debug\n"
    "-> or use the Makefile: make alchemy.debug\n"
  )
endif()

# warn if we're not using clang (even if toolchain was loaded, user might override)
# Alchemy is designed for clang but may work with GCC if stdlib matches LLVM
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING
    "alchemy::WARNING: not using clang compiler (detected: ${CMAKE_CXX_COMPILER_ID})\n"
    "-> Alchemy is designed for clang and may have issues with other compilers.\n"
    "-> clang provides better diagnostics and consistency with LLVM ecosystem.\n"
    "-> to install clang: run 'make setup'\n"
    "-> to use clang: run 'make alchemy.debug' (uses toolchain automatically)\n"
  )
endif()

# ============================================================================
# dependency discovery
# ============================================================================

# find LLVM (minimum version 12 -> lowest supported via Ubuntu 20.04)
# note: find_package in CONFIG mode looks for LLVMConfig.cmake
find_package(LLVM ${LLVM_PACKAGE_VERSION} CONFIG)

if(NOT LLVM_FOUND)
  message(FATAL_ERROR
    "\nalchemy::ERROR: LLVM not found!\n"
    "please install LLVM/clang before building.\n\n"
    "Linux (Ubuntu/Debian): sudo apt install clang libclang-dev llvm-dev\n"
    "macOS (Homebrew): brew install llvm\n"
    "Windows: install LLVM via official installer or Chocolatey.\n\n"
    "if LLVM is installed in a non-standard location, run CMake with:\n"
    "  cmake -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm ..\n"
    "-> or run make setup\n"
  )
endif()

if(ALCHEMY_LLVM_AUTODETECTED)
  message(WARNING "alchemy::auto-detected LLVM from ${LLVM_DIR} (set LLVM_DIR to override)")
endif()
message(STATUS "alchemy::found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "alchemy::using LLVMConfig.cmake in: ${LLVM_DIR}")

# find clang libraries
find_package(Clang CONFIG)

if(NOT Clang_FOUND)
  message(FATAL_ERROR
    "\nalchemy::ERROR: clang libraries not found!\n"
    "please install clang with C++ API support.\n\n"
    "Linux (Ubuntu/Debian): sudo apt install clang libclang-dev\n"
    "macOS (Homebrew): brew install llvm\n"
    "Windows: install LLVM/clang via official installer or Chocolatey.\n\n"
    "if clang is in a non-standard location, run CMake with:\n"
    "  cmake -DClang_DIR=/path/to/clang/lib/cmake/clang ..\n"
    "-> or run make setup\n"
  )
endif()

message(STATUS "alchemy::found clang libraries in: ${Clang_DIR}")

# find fmtlib for modern string formatting
# prefer system package (libfmt-dev on Linux, brew on macOS)
# fall back to FetchContent (Windows, or when system package is unavailable)
find_package(fmt 9 CONFIG)

if(fmt_FOUND)
  message(STATUS "alchemy::found fmt ${fmt_VERSION}")
else()
  message(STATUS "alchemy::fmt not found via system packages, fetching via FetchContent...")
  include(FetchContent)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        9.1.0
  )
  FetchContent_MakeAvailable(fmt)
endif()

# ============================================================================
# compiler flags
# ============================================================================

# common warning flags for all targets
# note: these are set via target_compile_options() below, not globally
set(ALCHEMY_WARNING_FLAGS
  -Wall -Wextra -Wpedantic
  -Werror
  -Wconversion -Wshadow -Wfloat-equal -Wundef -Wcast-align
  -Wuninitialized
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
  $<$<CXX_COMPILER_ID:GNU>:-Wmaybe-uninitialized>
  $<$<CXX_COMPILER_ID:Clang>:-Wconditional-uninitialized>
)

# debug-specific flags
# NOTE: CMake should add -g for Debug, but we explicitly add -O0 to ensure no optimizations
set(ALCHEMY_DEBUG_FLAGS
  -O0           # disable all optimizations (critical for debugging)
  -gdwarf-4     # valgrind compatibility (clang 14 defaults to dwarf-5)
)

# NOTE: no release-specific flags needed - CMake automatically adds -O3 -DNDEBUG

# performance test flags: always optimized regardless of build type
set(ALCHEMY_PERF_FLAGS
  -O3           # aggressive optimization
  -DNDEBUG      # disable assertions
  -g            # debug symbols for profiling
  -gdwarf-4     # valgrind compatibility (clang 14 defaults to dwarf-5)
)

# ============================================================================
# library target: alchemy_lib
# ============================================================================

add_library(alchemy_lib STATIC
  src/app/app.cpp
  src/cli/cli.cpp
  src/cli/config_parser.cpp
  src/files/discovery.cpp
  src/logger/logger.cpp
  src/operation/refactoring/salign_operation.cpp
  src/parsing/artifacts/artifacts.cpp
  src/parsing/libclang/clang_parser.cpp
  src/parsing/libclang/clang_struct_extractor.cpp
  src/parsing/libclang/clang_struct_parsing_rule.cpp
  src/parsing/libclang/compiler_adapters/clang_compilation_database_adapter.cpp
  src/parsing/libclang/compiler_adapters/clang_compilation_database_factory.cpp
  src/parsing/libclang/compiler_adapters/compiler_utils.cpp
  src/parsing/libclang/compiler_adapters/gcc_database_translator.cpp
  src/parsing/libclang/compiler_adapters/iar_database_translator.cpp
  src/parsing/libclang/compiler_adapters/msvc_database_translator.cpp
  src/pipeline/pipeline.cpp
  src/pipeline/preflight_validator.cpp
  src/reporting/metrics_reporter.cpp
  src/reporting/salign_reporter.cpp
  src/transmute/transmute.cpp
)

# public headers (consumers of alchemy_lib can #include from inc/)
target_include_directories(alchemy_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# system headers (suppress warnings from LLVM/clang headers)
# SYSTEM tells compiler these are third-party headers
target_include_directories(alchemy_lib
  SYSTEM PUBLIC
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# link against LLVM/clang libraries
target_link_libraries(alchemy_lib
  PUBLIC
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangTooling
    fmt::fmt
)

# compiler definitions from LLVM (e.g., -D__STDC_CONSTANT_MACROS)
target_compile_definitions(alchemy_lib
  PUBLIC
    ${LLVM_DEFINITIONS}
)

# compiler flags (warnings + optimization)
target_compile_options(alchemy_lib
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    $<$<CONFIG:Debug>:${ALCHEMY_DEBUG_FLAGS}>
)

# ============================================================================
# executable target: alchemy
# ============================================================================

add_executable(alchemy src/main.cpp)

target_link_libraries(alchemy
  PRIVATE
    alchemy_lib
)

target_compile_options(alchemy
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    $<$<CONFIG:Debug>:${ALCHEMY_DEBUG_FLAGS}>
)

# ============================================================================
# testing configuration
# ============================================================================

if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)
  message(STATUS "alchemy::found tests directory, configuring tests...")

  enable_testing()

  # configure CTest for better failure reporting
  set(CTEST_OUTPUT_ON_FAILURE ON)
  set(ENV{CTEST_OUTPUT_ON_FAILURE} 1)

  # add test subdirectory (contains test executables and GoogleTest dependency)
  add_subdirectory(tests)

  # ========================================================================
  # optional: code coverage (Clang only)
  # ========================================================================
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    option(ENABLE_COVERAGE "Enable code coverage" OFF)
    if(ENABLE_COVERAGE)
      message(STATUS "alchemy::code coverage enabled (--coverage)")
      target_compile_options(alchemy_lib PRIVATE --coverage)
      target_link_options(alchemy_lib PRIVATE --coverage)
    endif()
  endif()

  # ========================================================================
  # optional: sanitizers (debug builds only)
  # ========================================================================
  option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
  if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "alchemy::sanitizers enabled (ASan + UBSan)")
    target_compile_options(alchemy_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(alchemy_lib PUBLIC -fsanitize=address,undefined)
  endif()
endif()

# ============================================================================
# installation (future)
# ============================================================================
# TODO: add install() targets for packaging
# install(TARGETS alchemy DESTINATION bin)
# install(DIRECTORY inc/ DESTINATION include/alchemy)
