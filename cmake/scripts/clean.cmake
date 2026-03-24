# cmake/scripts/clean.cmake
# Portable cleanup script - removes build artifacts
# Usage: cmake -P cmake/scripts/clean.cmake

cmake_minimum_required(VERSION 3.21)

message(STATUS "alchemy::cleaning...")

# Get project root (two levels up from this script)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Remove build artifacts
file(REMOVE_RECURSE "${PROJECT_ROOT}/build")
file(REMOVE_RECURSE "${PROJECT_ROOT}/.cache")
file(REMOVE "${PROJECT_ROOT}/compile_commands.json")

message(STATUS "done")
