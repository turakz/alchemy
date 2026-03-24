cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # disable GNU extensions (-std=c++20, not -std=gnu++20)

# export compile_commands.json for tooling (clang-tidy, clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(mock_iccarm mock_iccarm.cpp)

target_link_libraries(mock_iccarm
  PRIVATE
    fmt::fmt
)

add_executable(mock_cl mock_cl.cpp)

target_link_libraries(mock_cl
  PRIVATE
    fmt::fmt
)
