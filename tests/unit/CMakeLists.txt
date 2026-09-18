message(STATUS "alchemy::configuring unit tests...")

add_executable(unit_tests
  test_app.cpp
  test_clang_parser.cpp
  test_clang_struct_parsing_rule.cpp
  test_clang_struct_extractor.cpp
  test_cli.cpp
  test_compilation_database_adapter.cpp
  test_config_parser.cpp
  test_core_types.cpp
  test_discovery.cpp
  test_operation.cpp
  test_pipeline.cpp
  test_pipeline_transmute.cpp
  test_preflight_validator.cpp
  test_salign_invariants.cpp
  test_salign_reporter.cpp
  test_transmute.cpp
)

target_include_directories(unit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(unit_tests PRIVATE
  alchemy_lib
  test_utils
  gtest_main
  gmock_main
)

# mock compilers must be built before unit tests can run
add_dependencies(unit_tests mock_iccarm mock_cl)

target_compile_options(unit_tests
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    $<$<CONFIG:Debug>:${ALCHEMY_DEBUG_FLAGS}>
)

# add coverage instrumentation if enabled
if(ENABLE_COVERAGE)
  target_compile_options(unit_tests PRIVATE --coverage)
  target_link_options(unit_tests PRIVATE --coverage)
endif()

# register unit tests with CTest
add_test(
  NAME UnitTests
  COMMAND unit_tests --gtest_color=yes --gtest_catch_exceptions=0 --gtest_print_time=1
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(UnitTests PROPERTIES
  # detect test failures via multiple signals
  FAIL_REGULAR_EXPRESSION "FAILED"

  # prevent gtest from catching exceptions/signals (exposes crashes clearly)
  ENVIRONMENT "GTEST_CATCH_EXCEPTIONS=0"

  # timeout for hung tests
  TIMEOUT 30

  # label for filtering
  LABELS "unit_tests"
)

message(STATUS "alchemy::unit tests configured")
