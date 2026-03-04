message(STATUS "alchemy::configuring integration tests...")

add_executable(integration_tests
  salign/test_salign_pipeline.cpp
  salign/test_salign_recipes.cpp
  salign/test_salign_errors.cpp
  test_clang_parser.cpp
)

target_include_directories(integration_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/tests/data
)

target_link_libraries(integration_tests
  PRIVATE
    alchemy_lib
    test_utils
    gtest_main
    gmock_main
)

# mock compilers must be built before integration tests can run
add_dependencies(integration_tests mock_iccarm mock_cl)

target_compile_options(integration_tests
  PRIVATE
    ${ALCHEMY_WARNING_FLAGS}
    $<$<CONFIG:Debug>:${ALCHEMY_DEBUG_FLAGS}>
)

target_compile_definitions(integration_tests
  PRIVATE
    ALCHEMY_BINARY_PATH="$<TARGET_FILE:alchemy>"
)

# add coverage instrumentation if enabled
if(ENABLE_COVERAGE)
  target_compile_options(integration_tests PRIVATE --coverage)
  target_link_options(integration_tests PRIVATE --coverage)
endif()

add_test(
  NAME IntegrationTests
  COMMAND integration_tests --gtest_color=yes --gtest_catch_exceptions=0 --gtest_print_time=1
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(IntegrationTests PROPERTIES
  # detect test failures
  FAIL_REGULAR_EXPRESSION "FAILED"

  # prevent gtest from catching exceptions/signals (exposes crashes clearly)
  ENVIRONMENT "ALCHEMY_TEST_DATA_DIR=${CMAKE_SOURCE_DIR}/tests/data;GTEST_CATCH_EXCEPTIONS=0"

  # timeout for hung tests
  TIMEOUT 30

  # label for filtering
  LABELS "integration_tests"
)

message(STATUS "alchemy::integration tests configured")
