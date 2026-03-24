// tests/unit/test_clang_struct_parsing_rule.cpp

// std
#include <memory>
#include <string>
#include <unordered_set>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <gtest/gtest.h>

// local
#include "parsing/libclang/clang_parsing_rules.hpp"
#include "parsing/libclang/clang_struct_parsing_rule.hpp"

namespace alchemy::testing {

class ClangStructParsingRuleTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    structParser = std::make_unique<alchemy::parser::ClangStructParsingRule>(
        std::unordered_set<std::string>{});
  }

  void
  TearDown() override
  {
    structParser.reset();
  }

  std::unique_ptr<alchemy::parser::ClangStructParsingRule> structParser;
};

TEST_F(ClangStructParsingRuleTest, ImplementsParsingRuleInterface)
{
  const std::string Name = structParser->getName();

  ASSERT_EQ(Name, "ClangStructParser")
      << "alchemy::testing::unit::parser name should match expected value";
}

TEST_F(ClangStructParsingRuleTest, InheritsFromClangParsingMatcher)
{
  alchemy::parser::ClangParsingMatcher* basePtr = structParser.get();

  ASSERT_NE(basePtr, nullptr) << "alchemy::testing::unit::should cast to base "
                                 "ClangParsingMatcher pointer";
  ASSERT_EQ(basePtr->getName(), "ClangStructParser")
      << "alchemy::testing::unit::polymorphic call should work correctly";
}

TEST_F(ClangStructParsingRuleTest, CanRegisterMatchers)
{
  clang::ast_matchers::MatchFinder finder;

  ASSERT_NO_THROW(structParser->registerMatchers(finder))
      << "alchemy::testing::unit::registerMatchers should not throw";
}

TEST_F(ClangStructParsingRuleTest, GetParsedStructsReturnsEmptyInitially)
{
  auto result = structParser->getParsedStructs();

  ASSERT_TRUE(result.empty())
      << "alchemy::testing::unit::initially should have no parsed structs";
  ASSERT_EQ(result.size(), 0) << "alchemy::testing::unit::size should be 0";
}

TEST_F(ClangStructParsingRuleTest, ErrorAccumulationStartsEmpty)
{
  ASSERT_FALSE(structParser->hasParseErrors())
      << "alchemy::testing::unit::initially should have no parse errors";
  ASSERT_TRUE(structParser->getParseErrors().empty())
      << "alchemy::testing::unit::error list should be empty";
}

}  // namespace alchemy::testing
