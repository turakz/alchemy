# ============================================================================
# Alchemy CMake Configuration
# ============================================================================
# minimum CMake version: 3.14 for FetchContent improvements
# (previously 3.2, but modern CMake features require >= 3.14)
cmake_minimum_required(VERSION 3.28)

# project declaration
project(alchemy
  LANGUAGES C CXX
  VERSION 1.0.0
  DESCRIPTION "c++ refactoring and code generation tool built on libclang/llvm"
)

# ============================================================================
# build configuration
# ============================================================================

# C++ standard: C++20 required for designated initializers, concepts, ranges, etc.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # disable GNU extensions (-std=c++20, not -std=gnu++20)

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
    "-> recommend running CMake with:\n"
    "   cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang.cmake\n"
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

# find LLVM (minimum version 10 for stable APIs, recommend 14+)
# note: find_package in CONFIG mode looks for LLVMConfig.cmake
find_package(LLVM REQUIRED CONFIG)

if(LLVM_FOUND)
  if(ALCHEMY_LLVM_AUTODETECTED)
    message(WARNING "alchemy::auto-detected LLVM from ${LLVM_DIR} (set LLVM_DIR to override)")
  endif()
  message(STATUS "alchemy::found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "alchemy::using LLVMConfig.cmake in: ${LLVM_DIR}")

  # optional: query LLVM's build configuration
  # useful for debugging ABI mismatch issues
  if(LLVM_TOOLS_BINARY_DIR)
    execute_process(
      COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --cxxflags
      OUTPUT_VARIABLE LLVM_CXX_FLAGS
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(LLVM_CXX_FLAGS)
      message(STATUS "alchemy::LLVM compiled with flags: ${LLVM_CXX_FLAGS}")

      # check for stdlib mismatch (libc++ vs libstdc++)
      # LLVM built with libc++ contains "-stdlib=libc++" in flags
      # we should match that in our build to avoid ABI issues
      if(LLVM_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+" AND
         NOT CMAKE_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+")
        message(WARNING
          "alchemy::WARNING: LLVM was built with libc++, but you're using libstdc++\n"
          "-> this WILL cause ABI compatibility issues (crashes, linker errors).\n"
          "-> to use libc++, uncomment the line in cmake/toolchains/clang.cmake\n"
          "-> and install libc++-dev: sudo apt install libc++-dev libc++abi-dev"
        )
      endif()
    endif()
  endif()
else()
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

# find clang libraries
find_package(Clang REQUIRED CONFIG)

if(Clang_FOUND)
  message(STATUS "alchemy::found clang libraries in: ${Clang_DIR}")
else()
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

# find fmtlib for modern string formatting
find_package(fmt REQUIRED CONFIG)

if(fmt_FOUND)
  message(STATUS "alchemy::found fmt ${fmt_VERSION}")
else()
  message(FATAL_ERROR
    "\nalchemy::ERROR: fmt library not found!\n"
    "please install it before building:\n"
    "Linux (Ubuntu/Debian): sudo apt install libfmt-dev\n"
    "macOS (Homebrew): brew install fmt\n"
    "Windows (Chocolatey): choco install fmt\n\n"
    "alternatively, use FetchContent to include fmt in the project.\n"
    "-> or run make setup\n"
  )
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

# NOTE: No release-specific flags needed - CMake automatically adds -O3 -DNDEBUG

# ============================================================================
# library target: alchemy_lib
# ============================================================================

add_library(alchemy_lib STATIC
  src/app/app.cpp
  src/cli/cli.cpp
  src/config/config.cpp
  src/files/discovery.cpp
  src/operation/refactoring/salign_operation.cpp
  src/parsing/artifacts/artifacts.cpp
  src/parsing/libclang/clang_parser.cpp
  src/parsing/libclang/clang_struct_extractor.cpp
  src/parsing/libclang/clang_struct_parsing_rule.cpp
  src/parsing/libclang/compiler_adapters/clang_compilation_database_adapter.cpp
  src/parsing/libclang/compiler_adapters/clang_compilation_database_factory.cpp
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
)

# system headers (suppress warnings from LLVM/clang headers)
# SYSTEM tells compiler these are third-party headers
target_include_directories(alchemy_lib
  SYSTEM PUBLIC
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
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
    -fdiagnostics-color=always  # colored compiler output
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
    -fdiagnostics-color=always
)

# ============================================================================
# testing configuration
# ============================================================================

if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)
  message(STATUS "alchemy::found tests directory, configuring tests...")

  # fetch GoogleTest from GitHub
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )

  # GoogleTest configuration
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  set(BUILD_GTEST ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)

  enable_testing()

  # configure CTest for better failure reporting
  set(CTEST_OUTPUT_ON_FAILURE ON)
  set(ENV{CTEST_OUTPUT_ON_FAILURE} 1)

  # add test subdirectory (contains test executables)
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
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ============================================================================
# installation (future)
# ============================================================================
# TODO: add install() targets for packaging
# install(TARGETS alchemy DESTINATION bin)
# install(DIRECTORY inc/ DESTINATION include/alchemy)
