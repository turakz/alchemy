message(STATUS "alchemy::configuring performance tests...")

add_executable(performance_tests
  test_salign.cpp
  test_salign_parsing.cpp
  test_salign_complexity.cpp
  test_salign_realistic.cpp
)

target_include_directories(performance_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(performance_tests
  PRIVATE
    alchemy_lib
    test_utils
    gtest_main
    gmock_main
)

target_compile_definitions(performance_tests
  PRIVATE
    ALCHEMY_BINARY_PATH="$<TARGET_FILE:alchemy>"
)

target_compile_options(performance_tests
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    -O3           # aggressive optimization
    -DNDEBUG      # disable assertions
    -g            # debug symbols for profiling
    -gdwarf-4     # valgrind compatibility (clang 14 defaults to dwarf-5)
    -fdiagnostics-color=always
)

add_test(
  NAME PerformanceTests
  COMMAND performance_tests --gtest_color=yes --gtest_catch_exceptions=0 --gtest_print_time=1
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(PerformanceTests PROPERTIES
  # detect test failures
  FAIL_REGULAR_EXPRESSION "FAILED"

  # prevent gtest from catching exceptions/signals (exposes crashes clearly)
  ENVIRONMENT "ALCHEMY_TEST_DATA_DIR=${CMAKE_SOURCE_DIR}/tests/data;GTEST_CATCH_EXCEPTIONS=0"

  # timeout for hung tests
  TIMEOUT 30

  # label for filtering
  LABELS "performance_tests"

  # ensure proper failure detection on segfaults/crashes
  WILL_FAIL FALSE
)

# stress tests - separate executable with longer timeout
add_executable(performance_stress_tests
  test_salign_stress.cpp
)

target_include_directories(performance_stress_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(performance_stress_tests
  PRIVATE
    alchemy_lib
    test_utils
    gtest_main
    gmock_main
)

target_compile_definitions(performance_stress_tests
  PRIVATE
    ALCHEMY_BINARY_PATH="$<TARGET_FILE:alchemy>"
)

target_compile_options(performance_stress_tests
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    -O3           # aggressive optimization
    -DNDEBUG      # disable assertions
    -g            # debug symbols for profiling
    -gdwarf-4     # valgrind compatibility (clang 14 defaults to dwarf-5)
    -fdiagnostics-color=always
)

add_test(
  NAME PerformanceStressTests
  COMMAND performance_stress_tests --gtest_color=yes --gtest_catch_exceptions=0 --gtest_print_time=1
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(PerformanceStressTests PROPERTIES
  # detect test failures
  FAIL_REGULAR_EXPRESSION "FAILED"

  # prevent gtest from catching exceptions/signals (exposes crashes clearly)
  ENVIRONMENT "ALCHEMY_TEST_DATA_DIR=${CMAKE_SOURCE_DIR}/tests/data;GTEST_CATCH_EXCEPTIONS=0"

  # timeout for hung tests (longer for stress tests)
  TIMEOUT 120

  # label for filtering
  LABELS "performance_stress_tests"

  # ensure proper failure detection on segfaults/crashes
  WILL_FAIL FALSE
)

message(STATUS "alchemy::performance tests configured")
