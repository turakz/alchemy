cmake_minimum_required(VERSION 3.2)

project(LightningStructs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optionally, set LLVM_DIR if CMake cannot find it automatically
# set(LLVM_DIR "/usr/local/opt/llvm/lib/cmake/llvm")

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
if(NOT LLVM_FOUND)
  message(FATAL_ERROR
      "\nLightningStructs::ERROR: LLVM not found!\n"
      "please install LLVM/Clang before building.\n\n"
      "Linux (Ubuntu/Debian): sudo apt install clang libclang-dev llvm-dev\n"
      "macOS (Homebrew): brew install llvm\n"
      "Windows: Install LLVM via official installer or Chocolatey.\n\n"
      "if LLVM is installed in a non-standard location, run CMake with:\n"
      "  cmake -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm .."
  )
else()
  message(STATUS "LightningStructs::found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "LightningStructs::using LLVMConfig.cmake in: ${LLVM_DIR}")
  # Include directories
  include_directories(LightningStructs
    SYSTEM PUBLIC
      ${LLVM_INCLUDE_DIRS}
  )
  add_definitions(${LLVM_DEFINITIONS})
endif()

# Find Clang
find_package(Clang CONFIG)
if(NOT Clang_FOUND)
    message(FATAL_ERROR
        "\nLightningStructs::ERROR: Clang not found!\n"
        "please install Clang with C++ API support.\n\n"
        "Linux (Ubuntu/Debian): sudo apt install clang libclang-dev\n"
        "macOS (Homebrew): brew install llvm\n"
        "Windows: Install LLVM/Clang via official installer or Chocolatey.\n\n"
        "if Clang is in a non-standard location, run CMake with:\n"
        "  cmake -DClang_DIR=/path/to/clang/lib/cmake/clang .."
    )
else()
  message(STATUS "LightningStructs::found Clang ${Clang_DIR}")
  include_directories(${Clang_INCLUDE_DIRS})
endif()

add_executable(LightningStructs
  lstructs_main.cpp
)
target_compile_options(LightningStructs
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wshadow
    -Wfloat-equal
    -Wundef
    -Wcast-align
)
target_include_directories(LightningStructs
  PUBLIC
    inc
)

if(LLVM_FOUND AND Clang_FOUND)
  # Link with Clang C++ API libraries
  target_link_libraries(LightningStructs
      PRIVATE
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangTooling
  )
endif()
