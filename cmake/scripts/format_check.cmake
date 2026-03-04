# cmake/scripts/format_check.cmake
# Portable format checking with clang-format (no modifications)
# Usage: cmake -P cmake/scripts/format_check.cmake

cmake_minimum_required(VERSION 3.14)

# Get source directory (project root)
if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

message(STATUS "alchemy::checking format...")

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
message(STATUS "  checking ${FILE_COUNT} files...")

# Check if clang-format is available
find_program(CLANG_FORMAT clang-format)
if(NOT CLANG_FORMAT)
  message(FATAL_ERROR "clang-format not found. Install it with 'make setup' or manually.")
endif()

# Check each file for formatting differences
set(FAILED_FILES "")
foreach(FILE ${ALL_FILES})
  # Run clang-format with --dry-run and --Werror
  # This outputs differences without modifying the file
  execute_process(
    COMMAND ${CLANG_FORMAT} --dry-run --Werror -style=file "${FILE}"
    RESULT_VARIABLE FORMAT_RESULT
    ERROR_VARIABLE FORMAT_ERROR
    OUTPUT_QUIET
  )

  if(NOT FORMAT_RESULT EQUAL 0)
    list(APPEND FAILED_FILES "${FILE}")
  endif()
endforeach()

# Report results
if(FAILED_FILES)
  list(LENGTH FAILED_FILES FAILED_COUNT)
  message(STATUS "")
  message(STATUS "alchemy::format check FAILED for ${FAILED_COUNT} file(s):")
  foreach(FILE ${FAILED_FILES})
    message(STATUS "  - ${FILE}")
  endforeach()
  message(STATUS "")
  message(STATUS "Run 'make format' to fix formatting issues.")
  message(FATAL_ERROR "alchemy::format check failed")
else()
  message(STATUS "✅ alchemy::format-check complete for ${FILE_COUNT} files(s)!")
endif()
