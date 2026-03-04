# cmake/scripts/doctor.cmake
# Environment diagnostics -> checks for required tools and configuration
# Usage: cmake -P cmake/scripts/doctor.cmake

cmake_minimum_required(VERSION 3.14)

message(STATUS "")
message(STATUS "==============================================")
message(STATUS "alchemy::doctor - Environment Diagnostics")
message(STATUS "==============================================")
message(STATUS "")

# Get project root
if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# Track overall health
set(ALL_OK TRUE)

# LLVM version suffix (passed via -DLLVM_VER=N from Makefile)
if(DEFINED LLVM_VER)
  set(_VER_SUFFIX "-${LLVM_VER}")
else()
  set(_VER_SUFFIX "")
endif()

# Check CMake version
message(STATUS "CMake:")
message(STATUS "  Version: ${CMAKE_VERSION}")
if(CMAKE_VERSION VERSION_LESS "3.14")
  message(STATUS "  Status: ⚠️  version < 3.14 (minimum 3.14 required)")
  set(ALL_OK FALSE)
else()
  message(STATUS "  Status: ✅ OK")
endif()
message(STATUS "")

# Check for Clang compiler
find_program(CLANG_EXECUTABLE NAMES clang++${_VER_SUFFIX} clang++)
message(STATUS "Clang++ Compiler:")
if(CLANG_EXECUTABLE)
  execute_process(
    COMMAND ${CLANG_EXECUTABLE} --version
    OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "clang version ([0-9]+\\.[0-9]+\\.[0-9]+)" CLANG_VERSION_MATCH "${CLANG_VERSION_OUTPUT}")
  if(CMAKE_MATCH_1)
    message(STATUS "  Version: ${CMAKE_MATCH_1}")
  endif()
  message(STATUS "  Path: ${CLANG_EXECUTABLE}")
  message(STATUS "  Status: ✅ OK")
else()
  message(STATUS "  Status: ⚠️  NOT FOUND on PATH")
  message(STATUS "  Note: CMake may find clang via toolchain or system packages")
  set(ALL_OK FALSE)
endif()
message(STATUS "")

# Check for LLVM via llvm-config (not available on Windows)
message(STATUS "LLVM:")
if(WIN32)
  message(STATUS "  Status: ⏭️  Skipped (llvm-config not available on Windows)")
  message(STATUS "  Note: LLVM is verified via clang++ check above")
else()
  find_program(LLVM_CONFIG NAMES llvm-config${_VER_SUFFIX} llvm-config)
  if(LLVM_CONFIG)
    execute_process(
      COMMAND ${LLVM_CONFIG} --version
      OUTPUT_VARIABLE LLVM_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    message(STATUS "  Version: ${LLVM_VERSION_OUTPUT}")
    message(STATUS "  Path: ${LLVM_CONFIG}")
    message(STATUS "  Status: ✅ OK")
  else()
    message(STATUS "  Status: ⚠️  NOT FOUND on PATH")
    message(STATUS "  Note: CMake may still find LLVM via LLVM_DIR or CMAKE_PREFIX_PATH")
    set(ALL_OK FALSE)
  endif()
endif()
message(STATUS "")

# Check for clang-format
find_program(CLANG_FORMAT NAMES clang-format${_VER_SUFFIX} clang-format)
message(STATUS "clang-format:")
if(CLANG_FORMAT)
  execute_process(
    COMMAND ${CLANG_FORMAT} --version
    OUTPUT_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "clang-format version ([0-9]+\\.[0-9]+\\.[0-9]+)" CLANG_FORMAT_VERSION_MATCH "${CLANG_FORMAT_VERSION_OUTPUT}")
  if(CMAKE_MATCH_1)
    message(STATUS "  Version: ${CMAKE_MATCH_1}")
  endif()
  message(STATUS "  Path: ${CLANG_FORMAT}")
  message(STATUS "  Status: ✅ OK")
else()
  message(STATUS "  Status: ⚠️  NOT FOUND on PATH")
  message(STATUS "  Install: run 'make setup' or manually install clang-format${_VER_SUFFIX}")
  set(ALL_OK FALSE)
endif()
message(STATUS "")

# Check for clang-tidy
find_program(CLANG_TIDY NAMES clang-tidy${_VER_SUFFIX} clang-tidy)
message(STATUS "clang-tidy:")
if(CLANG_TIDY)
  execute_process(
    COMMAND ${CLANG_TIDY} --version
    OUTPUT_VARIABLE CLANG_TIDY_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "LLVM version ([0-9]+\\.[0-9]+\\.[0-9]+)" CLANG_TIDY_VERSION_MATCH "${CLANG_TIDY_VERSION_OUTPUT}")
  if(CMAKE_MATCH_1)
    message(STATUS "  Version: ${CMAKE_MATCH_1}")
  endif()
  message(STATUS "  Path: ${CLANG_TIDY}")
  message(STATUS "  Status: ✅ OK")
else()
  message(STATUS "  Status: ⚠️  NOT FOUND on PATH")
  message(STATUS "  Install: run 'make setup' or manually install clang-tidy${_VER_SUFFIX}")
  set(ALL_OK FALSE)
endif()
message(STATUS "")

# Check for Ninja
find_program(NINJA_EXECUTABLE ninja)
message(STATUS "Ninja Build System:")
if(NINJA_EXECUTABLE)
  execute_process(
    COMMAND ${NINJA_EXECUTABLE} --version
    OUTPUT_VARIABLE NINJA_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message(STATUS "  Version: ${NINJA_VERSION}")
  message(STATUS "  Path: ${NINJA_EXECUTABLE}")
  message(STATUS "  Status: ✅ OK")
else()
  message(STATUS "  Status: ⚠️  NOT FOUND (optional but recommended)")
  message(STATUS "  Install: sudo apt install ninja-build (Linux) or brew install ninja (macOS)")
endif()
message(STATUS "")

# Check for bash (for parallel linting)
find_program(BASH_EXECUTABLE bash)
message(STATUS "Bash Shell:")
if(BASH_EXECUTABLE)
  execute_process(
    COMMAND ${BASH_EXECUTABLE} --version
    OUTPUT_VARIABLE BASH_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(REGEX MATCH "version ([0-9]+\\.[0-9]+\\.[0-9]+)" BASH_VERSION_MATCH "${BASH_VERSION_OUTPUT}")
  if(CMAKE_MATCH_1)
    message(STATUS "  Version: ${CMAKE_MATCH_1}")
  endif()
  message(STATUS "  Path: ${BASH_EXECUTABLE}")
  message(STATUS "  Status: ✅ OK")
  message(STATUS "  Note: Parallel linting will be available (4-8x faster)")
else()
  message(STATUS "  Status: ⚠️  NOT FOUND")
  message(STATUS "  Note: Sequential linting will be used")
  message(STATUS "  Install Git Bash for parallel linting: https://git-scm.com/downloads")
endif()
message(STATUS "")

# Check for required project files
message(STATUS "Project Files:")
set(REQUIRED_FILES
  ".clang-format"
  ".clang-tidy"
  "Makefile"
  "CMakeLists.txt"
)
set(FILES_OK TRUE)
foreach(FILE ${REQUIRED_FILES})
  if(EXISTS "${SOURCE_DIR}/${FILE}")
    message(STATUS "  ${FILE}: ✅")
  else()
    message(STATUS "  ${FILE}: ❌ MISSING")
    set(FILES_OK FALSE)
    set(ALL_OK FALSE)
  endif()
endforeach()
message(STATUS "")

# Final summary
message(STATUS "==============================================")
if(ALL_OK)
  message(STATUS "✅ All basic tools found on PATH!")
  message(STATUS "")
  message(STATUS "Next steps:")
  message(STATUS "  make alchemy.release      # Build the project")
  message(STATUS "  make test.all             # Run all tests")
else()
  message(STATUS "⚠️  Some tools not found on PATH")
  message(STATUS "")
  message(STATUS "Note: These are basic PATH checks. CMake may still find tools via:")
  message(STATUS "  - LLVM_DIR / Clang_DIR environment variables")
  message(STATUS "  - CMAKE_PREFIX_PATH")
  message(STATUS "  - System package configs (/usr/lib/cmake, etc.)")
  message(STATUS "")
  message(STATUS "Recommended: run 'make setup' and then re-run 'make doctor'")
endif()
message(STATUS "==============================================")
message(STATUS "")
