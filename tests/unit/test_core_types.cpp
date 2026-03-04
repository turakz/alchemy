// tests/unit/test_core_types.cpp

// std
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "app/core/core.hpp"

namespace alchemy::testing {

class ResultTypeTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
  }
};

TEST_F(ResultTypeTest, SuccessConstruction)
{
  auto result = alchemy::core::Result<std::string>::success("test_value");

  ASSERT_TRUE(result.valid());
  ASSERT_FALSE(result.invalid());
  ASSERT_EQ(result.value(), "test_value");
}

TEST_F(ResultTypeTest, FailureConstruction)
{
  auto result = alchemy::core::Result<std::string>::failure("error_message");

  ASSERT_FALSE(result.valid());
  ASSERT_TRUE(result.invalid());
  ASSERT_EQ(result.error(), "error_message");
}

TEST_F(ResultTypeTest, ValueAccessThrowsOnFailure)
{
  auto result = alchemy::core::Result<std::string>::failure("test_error");

  ASSERT_THROW(static_cast<void>(result.value()), std::bad_variant_access);
}

TEST_F(ResultTypeTest, ErrorAccessThrowsOnSuccess)
{
  auto result = alchemy::core::Result<std::string>::success("test_value");

  ASSERT_THROW(static_cast<void>(result.error()), std::bad_variant_access);
}

TEST_F(ResultTypeTest, TryValueReturnsOptional)
{
  auto successResult = alchemy::core::Result<std::string>::success("test");
  auto failureResult = alchemy::core::Result<std::string>::failure("error");

  auto successValue = successResult.tryValue();
  auto failureValue = failureResult.tryValue();

  ASSERT_TRUE(successValue.has_value());
  ASSERT_EQ(successValue->get(),
            "test");  // NOLINT(bugprone-unchecked-optional-access)

  ASSERT_FALSE(failureValue.has_value());
}

TEST_F(ResultTypeTest, moveSemantics)
{
  std::vector<std::string> largeData(1000, "item");
  auto result = alchemy::core::Result<std::vector<std::string>>::success(
      std::move(largeData));

  ASSERT_TRUE(result.valid());
  ASSERT_EQ(result.value().size(), 1000);
}

TEST_F(ResultTypeTest, RvalueValueExtraction)
{
  auto result = alchemy::core::Result<std::string>::success("test_value");

  // move value out using rvalue overload
  const std::string Extracted = std::move(result).value();

  ASSERT_EQ(Extracted, "test_value");
}

TEST_F(ResultTypeTest, RvalueErrorExtraction)
{
  auto result = alchemy::core::Result<std::string>::failure("error_message");

  // move error out using rvalue overload
  const std::string ExtractedError = std::move(result).error();

  ASSERT_EQ(ExtractedError, "error_message");
}

TEST_F(ResultTypeTest, RvalueValueFromTemporary)
{
  // test that rvalue overload is called on temporaries
  auto makeResult = []() {
    return alchemy::core::Result<std::string>::success("temporary_value");
  };

  const std::string Value = makeResult().value();  // calls && overload

  ASSERT_EQ(Value, "temporary_value");
}

TEST_F(ResultTypeTest, RvalueErrorFromTemporary)
{
  // test that rvalue overload is called on temporaries
  auto makeResult = []() {
    return alchemy::core::Result<std::string>::failure("temporary_error");
  };

  const std::string Error = makeResult().error();  // calls && overload

  ASSERT_EQ(Error, "temporary_error");
}

TEST_F(ResultTypeTest, LvalueReferencesStillWork)
{
  // verify lvalue overloads still work correctly
  auto result = alchemy::core::Result<std::string>::success("test_value");

  const std::string& valueRef = result.value();  // calls & overload

  ASSERT_EQ(valueRef, "test_value");
  ASSERT_EQ(result.value(), "test_value");  // result still valid
}

TEST_F(ResultTypeTest, moveExpensiveTypeFromResult)
{
  // test moving expensive types (avoiding copies)
  std::vector<int> largeVec(10000, 42);
  auto result =
      alchemy::core::Result<std::vector<int>>::success(std::move(largeVec));

  // Extract by moving
  std::vector<int> extracted = std::move(result).value();

  ASSERT_EQ(extracted.size(), 10000);
  ASSERT_EQ(extracted[0], 42);
}

}  // namespace alchemy::testing
