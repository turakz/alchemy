// tests/unit/test_pipeline.cpp
// unit tests for pipeline module
//
// NOTE: Tests for pipeline::transmute() and pipeline::execute() that involve
// actual filesystem I/O are integration tests located in
// tests/integration/salign/ for end-to-end validation.

// std
#include <chrono>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// 3rd party
#include <gtest/gtest.h>

// local
#include "app/core/core.hpp"
#include "metrics/metrics.hpp"
#include "metrics/salign_metrics.hpp"
#include "operation/operation_base.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/parser.hpp"
#include "parsing/parsing_requirements.hpp"
#include "pipeline/pipeline.hpp"

namespace alchemy::testing {

// mock parser for testing pipeline::runParser()
class MockParser : public alchemy::parser::ParsingRuleAdapter {
  bool m_shouldFail;
  std::string m_errorMessage;
  std::vector<alchemy::parser::artifacts::StructDef> m_mockStructs;

public:
  explicit MockParser(
      bool shouldFail = false,
      const std::string& errorMessage = "",
      std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {})
    : m_shouldFail(shouldFail),
      m_errorMessage(errorMessage),
      m_mockStructs(std::move(mockStructs))
  {
  }

  alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
  parse(const alchemy::parser::ParsingRequirements& requirements) override
  {
    if (m_shouldFail)
    {
      return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
          failure(alchemy::core::Error::format(
              "alchemy::parser::artifacts::test_pipeline.cpp",
              "MockOperation should fail: {}",
              m_errorMessage));
    }

    parser::artifacts::ParseResults results;
    if (requirements.needsStructParsing)
    {
      results.structs = m_mockStructs;
    }
    return alchemy::core::Result<
        alchemy::parser::artifacts::ParseResults>::success(std::move(results));
  }

  std::string_view
  getName() const override
  {
    return "MockParser";
  }

  std::vector<std::string_view>
  getSupportedExtensions() const override
  {
    return {".c", ".h", ".cpp", ".hpp"};  // default c/c++ family extensions
  }
};

// mock operation for testing
class MockOperation {
  std::string m_name;
  bool m_shouldFail;
  std::string m_errorMessage;
  alchemy::parser::ParsingRequirements m_requirements;

public:
  explicit MockOperation(const std::string& name,
                         bool shouldFail = false,
                         const std::string& errorMessage = "",
                         alchemy::parser::ParsingRequirements requirements =
                             alchemy::parser::ParsingRequirements{})
    : m_name(name),
      m_shouldFail(shouldFail),
      m_errorMessage(errorMessage),
      m_requirements(requirements)
  {
  }

  alchemy::parser::ParsingRequirements
  getRequirements() const
  {
    return m_requirements;
  }

  std::string
  getName() const
  {
    return m_name;
  }

  alchemy::core::Result<alchemy::operation::RecipeOperationResult>
  execute(const alchemy::parser::artifacts::ParseResults& /* artifacts */) const
  {
    if (m_shouldFail)
    {
      return alchemy::core::Result<alchemy::operation::RecipeOperationResult>::
          failure(alchemy::core::Error::format(
              "alchemy::operation::test_pipeline.cpp",
              "MockOperation should fail: {}",
              m_errorMessage));
    }

    alchemy::operation::RecipeOperationResult result;
    result.operationName = m_name;
    return alchemy::core::Result<
        alchemy::operation::RecipeOperationResult>::success(std::move(result));
  }
};

// mock operation that produces metrics
class MockOperationWithMetrics {
  std::string m_name;
  std::vector<alchemy::metrics::Metrics> m_metricsToReturn;
  alchemy::parser::ParsingRequirements m_requirements;

public:
  explicit MockOperationWithMetrics(
      const std::string& name,
      std::vector<alchemy::metrics::Metrics> metrics,
      alchemy::parser::ParsingRequirements requirements =
          alchemy::parser::ParsingRequirements{})
    : m_name(name),
      m_metricsToReturn(std::move(metrics)),
      m_requirements(requirements)
  {
  }

  // CRTP implementation methods (public, following LLVM convention)
  alchemy::parser::ParsingRequirements
  getRequirements() const
  {
    return m_requirements;
  }

  std::string
  getName() const
  {
    return m_name;
  }

  alchemy::core::Result<alchemy::operation::RecipeOperationResult>
  execute(const alchemy::parser::artifacts::ParseResults& /* artifacts */) const
  {
    alchemy::operation::RecipeOperationResult result;
    result.operationName = m_name;
    result.metrics = m_metricsToReturn;
    return alchemy::core::Result<
        alchemy::operation::RecipeOperationResult>::success(std::move(result));
  }
};

// mock operation that produces recipes
class MockOperationWithRecipes {
  std::string m_name;
  std::unordered_map<std::filesystem::path,
                     std::vector<alchemy::operation::Recipe>>
      m_recipesToReturn;
  alchemy::parser::ParsingRequirements m_requirements;

public:
  explicit MockOperationWithRecipes(
      const std::string& name,
      std::unordered_map<std::filesystem::path,
                         std::vector<alchemy::operation::Recipe>> recipes,
      alchemy::parser::ParsingRequirements requirements =
          alchemy::parser::ParsingRequirements{})
    : m_name(name),
      m_recipesToReturn(std::move(recipes)),
      m_requirements(requirements)
  {
  }

  // CRTP implementation methods (public, following LLVM convention)
  alchemy::parser::ParsingRequirements
  getRequirements() const
  {
    return m_requirements;
  }

  std::string
  getName() const
  {
    return m_name;
  }

  alchemy::core::Result<alchemy::operation::RecipeOperationResult>
  execute(const alchemy::parser::artifacts::ParseResults& /* artifacts */) const
  {
    alchemy::operation::RecipeOperationResult result;
    result.operationName = m_name;
    result.recipes = m_recipesToReturn;
    return alchemy::core::Result<
        alchemy::operation::RecipeOperationResult>::success(std::move(result));
  }
};

class PipelineTest : public ::testing::Test {
protected:
  void
  SetUp() override
  {
    tempDir = std::filesystem::temp_directory_path() / "alchemy_pipeline_test" /
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(tempDir);
  }

  void
  TearDown() override
  {
    if (std::filesystem::exists(tempDir))
    {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::filesystem::path tempDir;
};

// test-specific variant that includes mock operations
using TestRecipeOperation = std::
    variant<MockOperation, MockOperationWithMetrics, MockOperationWithRecipes>;

// test pipeline::executeOperations with empty operations list
TEST_F(PipelineTest, ExecuteOperationsHandlesEmptyList)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  const std::vector<alchemy::testing::TestRecipeOperation> Operations;

  ASSERT_TRUE(Operations.empty());

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, Operations);

  ASSERT_TRUE(results.empty()) << "alchemy::testing::unit::empty operations "
                                  "should produce empty results";
}

// test pipeline::executeOperations with single successful operation
TEST_F(PipelineTest, ExecuteOperationsSingleSuccess)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  operations.emplace_back(MockOperation("TestOp"));

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, operations);

  ASSERT_EQ(results.size(), 1)
      << "alchemy::testing::unit::should have one result";
  ASSERT_TRUE(results[0].valid())
      << "alchemy::testing::unit::operation should succeed";
  ASSERT_EQ(results[0].value().operationName, "TestOp")
      << "alchemy::testing::unit::operation name should match";
}

// test pipeline::executeOperations with multiple successful operations
TEST_F(PipelineTest, ExecuteOperationsMultipleSuccess)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  operations.emplace_back(MockOperation("Op1"));
  operations.emplace_back(MockOperation("Op2"));
  operations.emplace_back(MockOperation("Op3"));

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, operations);

  ASSERT_EQ(results.size(), 3)
      << "alchemy::testing::unit::should have three results";
  ASSERT_TRUE(results[0].valid())
      << "alchemy::testing::unit::Op1 should succeed";
  ASSERT_TRUE(results[1].valid())
      << "alchemy::testing::unit::Op2 should succeed";
  ASSERT_TRUE(results[2].valid())
      << "alchemy::testing::unit::Op3 should succeed";
  ASSERT_EQ(results[0].value().operationName, "Op1");
  ASSERT_EQ(results[1].value().operationName, "Op2");
  ASSERT_EQ(results[2].value().operationName, "Op3");
}

// test pipeline::executeOperations handles single operation failure
TEST_F(PipelineTest, ExecuteOperationsHandlesSingleFailure)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  operations.emplace_back(MockOperation("FailOp", true, "operation failed"));

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, operations);

  ASSERT_EQ(results.size(), 1)
      << "alchemy::testing::unit::should have one result";
  ASSERT_TRUE(results[0].invalid())
      << "alchemy::testing::unit::operation should fail";
  ASSERT_NE(results[0].error().find("operation failed"), std::string::npos)
      << "alchemy::testing::unit::error message should be preserved";
}

// test pipeline::executeOperations does not stop on first failure (returns all
// results)
TEST_F(PipelineTest, ExecuteOperationsContinuesAfterFailure)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  operations.emplace_back(MockOperation("Op1"));  // succeeds
  operations.emplace_back(MockOperation("Op2", true, "op2 failed"));  // fails
  operations.emplace_back(MockOperation("Op3"));  // should still execute

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, operations);

  ASSERT_EQ(results.size(), 3) << "alchemy::testing::unit::all operations "
                                  "should execute even after failure";
  ASSERT_TRUE(results[0].valid())
      << "alchemy::testing::unit::Op1 should succeed";
  ASSERT_TRUE(results[1].invalid())
      << "alchemy::testing::unit::Op2 should fail";
  ASSERT_TRUE(results[2].valid())
      << "alchemy::testing::unit::Op3 should still execute and succeed";
}

// test pipeline::executeOperations with mixed success/failure
TEST_F(PipelineTest, ExecuteOperationsHandlesMixedResults)
{
  const alchemy::parser::artifacts::ParseResults Artifacts;
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  operations.emplace_back(MockOperation("Success1"));
  operations.emplace_back(MockOperation("Fail1", true, "first failure"));
  operations.emplace_back(MockOperation("Success2"));
  operations.emplace_back(MockOperation("Fail2", true, "second failure"));

  auto results = alchemy::pipeline::executeOperations<
      alchemy::testing::TestRecipeOperation>(Artifacts, operations);

  ASSERT_EQ(results.size(), 4)
      << "alchemy::testing::unit::should have all four results";
  ASSERT_TRUE(results[0].valid());
  ASSERT_TRUE(results[1].invalid());
  ASSERT_TRUE(results[2].valid());
  ASSERT_TRUE(results[3].invalid());
  ASSERT_EQ(results[0].value().operationName, "Success1");
  ASSERT_EQ(results[2].value().operationName, "Success2");
}

// test pipeline::runParser() with mock parser (success case)
TEST_F(PipelineTest, ParseWithMockParserCHeaderSuccess)
{
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(MockOperation("TestOp", false, "", requirements));

  auto result =
      alchemy::pipeline::runParser<alchemy::testing::TestRecipeOperation>(
          parser, operations);

  ASSERT_TRUE(result.valid()) << "alchemy::testing::unit::parse c header "
                                 "should succeed with valid mock parser";
  ASSERT_FALSE(result.value().structs.empty())
      << "alchemy::testing::unit::parse c header should populate artifacts "
         "with structs";
}

// test pipeline::runParser() handles parser errors
TEST_F(PipelineTest, ParseWithMockParserHandlesErrors)
{
  MockParser parser(true, "mock parser error");

  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(MockOperation("TestOp", false, "", requirements));

  auto result =
      alchemy::pipeline::runParser<alchemy::testing::TestRecipeOperation>(
          parser, operations);

  ASSERT_TRUE(result.invalid())
      << "alchemy::testing::unit::parse should propagate parser errors";
  ASSERT_NE(result.error().find("mock parser error"), std::string::npos)
      << "alchemy::testing::unit::error message should be preserved";
}

// ============================================================================
// tests for pipeline::execute() - full pipeline orchestration
// ============================================================================

// test pipeline::execute() with successful end-to-end flow
TEST_F(PipelineTest, ExecutePipelineSuccessfulCHeadersFlow)
{
  // setup: create mock parser with test data
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  // setup: create operation
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(MockOperation("TestOp", false, "", requirements));

  // execute full pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, true);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::c headers pipeline should succeed";
  ASSERT_TRUE(result.value().allMetrics.empty())
      << "alchemy::testing::unit::mock operation produces no metrics";
}

// test pipeline::execute() handles parsing failures
TEST_F(PipelineTest, ExecutePipelineHandlesParsingFailure)
{
  // setup: create failing parser
  MockParser parser(true, "parse error");

  // setup: create operation
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(MockOperation("TestOp", false, "", requirements));

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, false);

  ASSERT_TRUE(!result.valid())
      << "alchemy::testing::unit::pipeline result should indicate "
         "failure";
}

// test pipeline::execute() handles operation failures
TEST_F(PipelineTest, ExecutePipelineHandlesCHeadersOperationFailure)
{
  // setup: create successful parser
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  // setup: create failing operation
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(
      MockOperation("FailOp", true, "operation failed", requirements));

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, false);

  ASSERT_TRUE(!result.valid())
      << "alchemy::testing::unit::c headers pipeline result should indicate "
         "failure";
}

// test pipeline::execute() with multiple operations (mixed success/failure)
TEST_F(PipelineTest, ExecutePipelineWithMultipleOperations)
{
  // setup: create successful parser
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  // setup: create multiple operations (one succeeds, one fails)
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(MockOperation("SuccessOp", false, "", requirements));
  operations.emplace_back(
      MockOperation("FailOp", true, "op2 failed", requirements));

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, false);

  ASSERT_TRUE(!result.valid()) << "alchemy::testing::unit::pipeline should "
                                  "indicate failure even if partial failures";
}

// test pipeline::execute() with empty operations list
TEST_F(PipelineTest, ExecutePipelineWithEmptyOperations)
{
  // setup: create parser (won't be called since no operations)
  MockParser parser(false, "");

  // setup: empty operations
  const std::vector<alchemy::testing::TestRecipeOperation> Operations;

  ASSERT_TRUE(Operations.empty());

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, Operations, tempDir, false);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::pipeline should handle empty operations";
  ASSERT_TRUE(result.value().allMetrics.empty());
}

// test pipeline::execute() propagates metrics from operation to PipelineResult
TEST_F(PipelineTest, ExecutePipelinePropagatesMetrics)
{
  // setup: create parser with test data
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  // setup: create mock metrics
  alchemy::metrics::detail::SAlignMetrics metric;
  metric.structName = "TestStruct";
  metric.sourceFile = "test.h";
  metric.naturalTotalSize = 16;
  metric.optimizedSize = 12;
  metric.possibleSavings = 4;

  const std::vector<alchemy::metrics::Metrics> Metrics = {metric};

  // setup: create operation that produces metrics
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;
  operations.emplace_back(
      MockOperationWithMetrics("MetricsOp", Metrics, requirements));

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, true);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::pipeline should succeed";
  ASSERT_EQ(result.value().allMetrics.size(), 1)
      << "alchemy::testing::unit::metrics from operation should appear in "
         "result";

  // verify the metric is the one we created
  ASSERT_TRUE(std::holds_alternative<alchemy::metrics::detail::SAlignMetrics>(
      result.value().allMetrics[0]))
      << "alchemy::testing::unit::metric should be SAlignMetrics variant";
  auto& returnedMetric = std::get<alchemy::metrics::detail::SAlignMetrics>(
      result.value().allMetrics[0]);
  ASSERT_EQ(returnedMetric.structName, "TestStruct");
  ASSERT_EQ(returnedMetric.possibleSavings, 4);

  // verify summary is initialized to zero on dry-run
  ASSERT_EQ(result.value().summary.recipesApplied, 0)
      << "alchemy::testing::unit::dry-run should initialize summary to zero";
  ASSERT_EQ(result.value().summary.filesProcessed, 0)
      << "alchemy::testing::unit::dry-run should initialize summary to zero";
}

// test pipeline::execute() aggregates metrics from multiple operations
TEST_F(PipelineTest, ExecuteCHeadersPipelineAggregatesMultipleOperationMetrics)
{
  // setup: create parser with test data
  auto mockStruct =
      alchemy::parser::artifacts::StructDef("TestStruct", "test.h");
  std::vector<alchemy::parser::artifacts::StructDef> mockStructs = {
      std::move(mockStruct)};
  MockParser parser(false, "", std::move(mockStructs));

  // setup: create metrics for first operation
  alchemy::metrics::detail::SAlignMetrics metric1;
  metric1.structName = "Struct1";
  metric1.sourceFile = "test1.h";
  metric1.possibleSavings = 4;

  // setup: create metrics for second operation
  alchemy::metrics::detail::SAlignMetrics metric2;
  metric2.structName = "Struct2";
  metric2.sourceFile = "test2.h";
  metric2.possibleSavings = 8;

  // setup: create two operations with different metrics
  std::vector<alchemy::testing::TestRecipeOperation> operations;
  alchemy::parser::ParsingRequirements requirements;
  requirements.needsStructParsing = true;

  const std::vector<alchemy::metrics::Metrics> Metrics1 = {metric1};
  const std::vector<alchemy::metrics::Metrics> Metrics2 = {metric2};

  operations.emplace_back(
      MockOperationWithMetrics("Op1", Metrics1, requirements));
  operations.emplace_back(
      MockOperationWithMetrics("Op2", Metrics2, requirements));

  // execute pipeline
  auto result =
      alchemy::pipeline::execute<alchemy::testing::TestRecipeOperation>(
          parser, operations, tempDir, true);

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::pipeline should succeed";
  ASSERT_EQ(result.value().allMetrics.size(), 2)
      << "alchemy::testing::unit::metrics from both operations should be "
         "aggregated";

  // verify both metrics are present
  auto& m1 = std::get<alchemy::metrics::detail::SAlignMetrics>(
      result.value().allMetrics[0]);
  auto& m2 = std::get<alchemy::metrics::detail::SAlignMetrics>(
      result.value().allMetrics[1]);
  ASSERT_EQ(m1.structName, "Struct1");
  ASSERT_EQ(m2.structName, "Struct2");
}

}  // namespace alchemy::testing
