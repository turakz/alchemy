# cmake/scripts/check_compile_commands.cmake
# Helper script to check if compile_commands.json exists
# Usage: cmake -P cmake/scripts/check_compile_commands.cmake

cmake_minimum_required(VERSION 3.14)

# Get project root
if(NOT DEFINED SOURCE_DIR)
  get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# Check for compile_commands.json in multiple locations
set(COMPILE_COMMANDS_FOUND FALSE)
set(COMPILE_COMMANDS_LOCATIONS
  "${SOURCE_DIR}/build/compile_commands.json"
)

foreach(LOCATION ${COMPILE_COMMANDS_LOCATIONS})
  if(EXISTS "${LOCATION}")
    set(COMPILE_COMMANDS_FOUND TRUE)
    message(STATUS "alchemy::found compile_commands.json at ${LOCATION}")
    break()
  endif()
endforeach()

if(NOT COMPILE_COMMANDS_FOUND)
  message(STATUS "")
  message(STATUS "❌ compile_commands.json not found")
  message(STATUS "")
  message(STATUS "clang-tidy requires a compilation database to analyze your code.")
  message(STATUS "")
  message(STATUS "To generate it, build the project first:")
  message(STATUS "  make alchemy.release or make alchemy.debug")
  message(STATUS "")
  message(STATUS "This will create compile_commands.json automatically.")
  message(STATUS "")
  message(FATAL_ERROR "compile_commands.json not found - build the project first")
endif()
