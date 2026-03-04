# cmake/scripts/format.cmake
# Portable source code formatting with clang-format
# Usage: cmake -P cmake/scripts/format.cmake

cmake_minimum_required(VERSION 3.14)

# Get source directory (project root)
if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

message(STATUS "alchemy::formatting source code...")

# Find all source files
file(GLOB_RECURSE HPP_FILES
  "${SOURCE_DIR}/inc/*.hpp"
)

file(GLOB_RECURSE CPP_FILES
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/tests/*.cpp"
)

list(APPEND ALL_FILES ${HPP_FILES} ${CPP_FILES})

# Add main.cpp if it exists
if(EXISTS "${SOURCE_DIR}/main.cpp")
  list(APPEND ALL_FILES "${SOURCE_DIR}/main.cpp")
endif()

list(LENGTH ALL_FILES FILE_COUNT)
message(STATUS "  formatting ${FILE_COUNT} files...")

# Check if clang-format is available (prefer versioned binary if passed via -DCLANG_FORMAT_BIN=...)
if(DEFINED CLANG_FORMAT_BIN)
  find_program(CLANG_FORMAT "${CLANG_FORMAT_BIN}")
else()
  find_program(CLANG_FORMAT clang-format)
endif()
if(NOT CLANG_FORMAT)
  message(FATAL_ERROR "clang-format not found. Install it with 'make setup' or manually.")
endif()

# Format each file
set(FAILED_FILES "")
foreach(FILE ${ALL_FILES})
  execute_process(
    COMMAND ${CLANG_FORMAT} -i -style=file "${FILE}"
    RESULT_VARIABLE FORMAT_RESULT
    ERROR_VARIABLE FORMAT_ERROR
    OUTPUT_QUIET
  )

  if(NOT FORMAT_RESULT EQUAL 0)
    list(APPEND FAILED_FILES "${FILE}")
    message(WARNING "Failed to format: ${FILE}\n  ${FORMAT_ERROR}")
  endif()
endforeach()

# Report results
if(FAILED_FILES)
  list(LENGTH FAILED_FILES FAILED_COUNT)
  message(FATAL_ERROR "alchemy::formatting failed for ${FAILED_COUNT} file(s)")
else()
  message(STATUS "✅ alchemy::format complete for ${FILE_COUNT} files(s)!")
endif()
