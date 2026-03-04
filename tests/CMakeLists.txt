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

# tests/CMakeLists.txt
message(STATUS "alchemy::tests::configuring...")

# mock compilers
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/mock_compilers)
message(STATUS "alchemy::found mock compilers for testing, adding subdirectory")
  add_subdirectory(data/mock_compilers)
endif()

# unit tests
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/unit)
  message(STATUS "alchemy::found unit tests directory, adding subdirectory")
  add_subdirectory(unit)
else()
  message(STATUS "alchemy::no unit tests directory found")
endif()

# integration tests
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
  message(STATUS "alchemy::found integration tests directory, adding subdirectory")
  add_subdirectory(integration)
else()
  message(STATUS "alchemy::no integration tests directory found")
endif()

message(STATUS "alchemy::tests::configuration complete")

# performance tests
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/performance)
  message(STATUS "alchemy::found performance tests directory, adding subdirectory")
  add_subdirectory(performance)
else()
  message(STATUS "alchemy::no performance tests directory found")
endif()

message(STATUS "alchemy::tests::configuration complete")
