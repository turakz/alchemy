// tests/data/integration/salign/salign_test_fixture.hpp
// shared test fixture for salign integration tests

#ifndef ALCHEMY_TESTS_DATA_INTEGRATION_SALIGN_SALIGN_TEST_FIXTURE_HPP
#define ALCHEMY_TESTS_DATA_INTEGRATION_SALIGN_SALIGN_TEST_FIXTURE_HPP

// std
#include <filesystem>

// 3rd party
#include <gtest/gtest.h>

// local
#include "utils.hpp"

namespace alchemy::testing {

class SalignIntegrationTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    // create temporary test directory
    tempDir = alchemy::testing::utils::createTempTestDirectory(
        "alchemy_salign_test");
  }

  void
  TearDown() override
  {
    // cleanup temporary directory
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
};

}  // namespace alchemy::testing

#endif  // ALCHEMY_TESTS_DATA_INTEGRATION_SALIGN_SALIGN_TEST_FIXTURE_HPP
