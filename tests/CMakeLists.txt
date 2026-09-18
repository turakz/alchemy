# fetch GoogleTest from GitHub (v1.14.0)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(BUILD_GTEST ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# shared test utilities library
add_library(test_utils STATIC
  utils.cpp
)

target_include_directories(test_utils
  PUBLIC
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(test_utils
  PUBLIC
    alchemy_lib
)

# inject absolute paths to mock compiler binaries so tests work from any CWD
target_compile_definitions(test_utils
  PUBLIC
    MOCK_ICCARM_PATH="$<TARGET_FILE:mock_iccarm>"
    MOCK_CL_PATH="$<TARGET_FILE:mock_cl>"
)

message(STATUS "alchemy::configuring tests...")

add_subdirectory(data/mock_compilers)
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(performance)

message(STATUS "alchemy::tests configured")
