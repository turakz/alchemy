// tests/unit/test_struct_extractor.cpp

// std
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// 3rd party
#include <clang/AST/Decl.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <gtest/gtest.h>

// local
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_struct_extractor.hpp"

namespace alchemy::testing {

// Helper class to build minimal AST for testing
class StructExtractorTest : public ::testing::Test {
protected:
  // Helper to build AST from source code
  std::unique_ptr<clang::ASTUnit>
  buildAST(const std::string& code)
  {
    const std::vector<std::string> Args = {"-std=c11"};
    return clang::tooling::buildASTFromCodeWithArgs(code, Args, "test.c");
  }

  // Helper to find first RecordDecl in AST
  const clang::RecordDecl*
  findFirstStruct(clang::ASTUnit* ast) const
  {
    const clang::RecordDecl* result = nullptr;

    clang::ast_matchers::MatchFinder finder;
    auto matcher =
        clang::ast_matchers::recordDecl(clang::ast_matchers::isStruct(),
                                        clang::ast_matchers::isDefinition())
            .bind("struct");

    class StructFinder
      : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
      const clang::RecordDecl** target;
      explicit StructFinder(const clang::RecordDecl** t) : target(t)
      {
      }
      void
      run(const clang::ast_matchers::MatchFinder::MatchResult& result) override
      {
        if (*target == nullptr)
        {
          *target = result.Nodes.getNodeAs<clang::RecordDecl>("struct");
        }
      }
    };

    StructFinder callback(&result);
    finder.addMatcher(matcher, &callback);
    finder.matchAST(ast->getASTContext());

    return result;
  }

  // Helper to find first FieldDecl in RecordDecl
  const clang::FieldDecl*
  findFirstField(const clang::RecordDecl* record)
  {
    for (const clang::FieldDecl* field : record->fields())
    {
      return field;
    }
    return nullptr;
  }

  // Helper to validate pointer field properties
  void
  validatePointerField(const alchemy::parser::artifacts::FieldDef& fieldDef)
  {
    ASSERT_TRUE(fieldDef.canReorder)
        << "alchemy::testing::unit::pointers can be reordered";
    ASSERT_FALSE(fieldDef.isBitField)
        << "alchemy::testing::unit::pointers are not bitfields";
    ASSERT_GT(fieldDef.naturalSize, 0U);
  }
};

// Test extractStruct with simple struct
TEST_F(StructExtractorTest, ExtractStructSimpleCase)
{
  const std::string Code = R"(
    struct SimpleStruct {
      int x;
      double y;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  auto result = alchemy::parser::StructExtractor::extractStruct(
      structDecl, ast->getSourceManager(), ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::extraction should succeed";

  const auto& structDef = result.value();
  ASSERT_EQ(structDef.structName, "SimpleStruct");
  ASSERT_EQ(structDef.getFieldCount(), 2);
  ASSERT_GT(structDef.naturalTotalSize, 0);
  ASSERT_GT(structDef.currentDataSize, 0);
  ASSERT_GT(structDef.naturalAlignment, 0);
}

// Test extractStruct with empty struct
TEST_F(StructExtractorTest, ExtractStructEmptyStruct)
{
  const std::string Code = R"(
    struct EmptyStruct {
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  auto result = alchemy::parser::StructExtractor::extractStruct(
      structDecl, ast->getSourceManager(), ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::empty struct extraction should succeed";

  const auto& structDef = result.value();
  ASSERT_EQ(structDef.structName, "EmptyStruct");
  ASSERT_EQ(structDef.getFieldCount(), 0);
  ASSERT_EQ(structDef.currentDataSize, 0);
}

// Test extractStruct detects bitfields
TEST_F(StructExtractorTest, ExtractStructDetectsBitfields)
{
  const std::string Code = R"(
    struct BitfieldStruct {
      unsigned int flag1 : 1;
      unsigned int flag2 : 1;
      int normalField;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  auto result = alchemy::parser::StructExtractor::extractStruct(
      structDecl, ast->getSourceManager(), ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::bitfield struct extraction should succeed";

  const auto& structDef = result.value();
  ASSERT_EQ(structDef.structName, "BitfieldStruct");
  ASSERT_TRUE(structDef.hasBitFields)
      << "alchemy::testing::unit::should detect bitfields";
  ASSERT_EQ(structDef.getFieldCount(), 3);
}

// Test extractStruct calculates layout correctly
TEST_F(StructExtractorTest, ExtractStructCalculatesLayoutCorrectly)
{
  const std::string Code = R"(
    struct UnoptimizedStruct {
      char small;
      double large;
      char small2;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  auto result = alchemy::parser::StructExtractor::extractStruct(
      structDecl, ast->getSourceManager(), ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::extraction should succeed";

  const auto& structDef = result.value();
  ASSERT_EQ(structDef.structName, "UnoptimizedStruct");
  ASSERT_EQ(structDef.getFieldCount(), 3);

  // This struct should have significant padding
  ASSERT_GT(structDef.naturalTotalSize, structDef.currentDataSize)
      << "alchemy::testing::unit::unoptimized struct should have padding";
  ASSERT_GT(structDef.currentWastedBytes, 0)
      << "alchemy::testing::unit::should calculate wasted bytes";

  // Data size should be 1 + 8 + 1 = 10 bytes (char + double + char)
  ASSERT_EQ(structDef.currentDataSize, 10);
}

// Test extractField with simple field
TEST_F(StructExtractorTest, ExtractFieldSimpleCase)
{
  const std::string Code = R"(
    struct TestStruct {
      int field;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  const clang::FieldDecl* fieldDecl = findFirstField(structDecl);
  ASSERT_NE(fieldDecl, nullptr);

  auto result =
      alchemy::parser::StructExtractor::extractField(fieldDecl,
                                                     ast->getASTContext(),
                                                     ast->getSourceManager(),
                                                     ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::field extraction should succeed";

  const auto& fieldDef = result.value();
  ASSERT_EQ(fieldDef.fieldName, "field");
  ASSERT_EQ(fieldDef.typeName, "int");
  ASSERT_EQ(fieldDef.naturalSize, 4);  // int is 4 bytes
  ASSERT_EQ(fieldDef.naturalAlignment, 4);
  ASSERT_FALSE(fieldDef.isBitField);
  ASSERT_TRUE(fieldDef.canReorder);
}

// Test extractField with bitfield
TEST_F(StructExtractorTest, ExtractFieldHandlesBitfield)
{
  const std::string Code = R"(
    struct TestStruct {
      unsigned int flag : 1;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  const clang::FieldDecl* fieldDecl = findFirstField(structDecl);
  ASSERT_NE(fieldDecl, nullptr);

  auto result =
      alchemy::parser::StructExtractor::extractField(fieldDecl,
                                                     ast->getASTContext(),
                                                     ast->getSourceManager(),
                                                     ast->getLangOpts());

  ASSERT_TRUE(result.valid())
      << "alchemy::testing::unit::bitfield extraction should succeed";

  const auto& fieldDef = result.value();
  ASSERT_EQ(fieldDef.fieldName, "flag");
  ASSERT_TRUE(fieldDef.isBitField)
      << "alchemy::testing::unit::should detect bitfield";
  ASSERT_FALSE(fieldDef.canReorder)
      << "alchemy::testing::unit::bitfields cannot be reordered";
}

// Test extractField determines reorderability
TEST_F(StructExtractorTest, ExtractFieldDeterminesReorderability)
{
  const std::string Code = R"(
    struct TestStruct {
      int normalField;
      unsigned int bitfield : 3;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  // Test normal field
  auto fields = structDecl->fields();
  auto it = fields.begin();
  const clang::FieldDecl* normalFieldDecl = *it;
  ++it;
  const clang::FieldDecl* bitfieldDecl = *it;

  auto normalResult =
      alchemy::parser::StructExtractor::extractField(normalFieldDecl,
                                                     ast->getASTContext(),
                                                     ast->getSourceManager(),
                                                     ast->getLangOpts());

  ASSERT_TRUE(normalResult.valid());
  ASSERT_TRUE(normalResult.value().canReorder)
      << "alchemy::testing::unit::normal fields can be reordered";

  auto bitfieldResult =
      alchemy::parser::StructExtractor::extractField(bitfieldDecl,
                                                     ast->getASTContext(),
                                                     ast->getSourceManager(),
                                                     ast->getLangOpts());

  ASSERT_TRUE(bitfieldResult.valid());
  ASSERT_FALSE(bitfieldResult.value().canReorder)
      << "alchemy::testing::unit::bitfields cannot be reordered";
}

// Test extractField calculates offsets correctly
TEST_F(StructExtractorTest, ExtractFieldCalculatesOffsetsCorrectly)
{
  const std::string Code = R"(
    struct TestStruct {
      int field;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  const clang::FieldDecl* fieldDecl = findFirstField(structDecl);
  ASSERT_NE(fieldDecl, nullptr);

  auto result =
      alchemy::parser::StructExtractor::extractField(fieldDecl,
                                                     ast->getASTContext(),
                                                     ast->getSourceManager(),
                                                     ast->getLangOpts());

  ASSERT_TRUE(result.valid());

  const auto& fieldDef = result.value();
  // Byte offset should be valid (>= 0)
  ASSERT_GE(fieldDef.byteOffset, 0U);
  // Byte length should be positive
  ASSERT_GT(fieldDef.byteLength, 0U);
}

// Test extractField handles pointer types
TEST_F(StructExtractorTest, ExtractFieldHandlesPointerTypes)
{
  const std::string Code = R"(
    struct TestStruct {
      int* ptr;
      char* charPtr;
      void* voidPtr;
    };
  )";

  auto ast = buildAST(Code);
  ASSERT_NE(ast, nullptr);

  const clang::RecordDecl* structDecl = findFirstStruct(ast.get());
  ASSERT_NE(structDecl, nullptr);

  // Extract all pointer fields
  auto fields = structDecl->fields();
  ASSERT_EQ(std::distance(fields.begin(), fields.end()), 3)
      << "alchemy::testing::unit::should have 3 pointer fields";

  for (const clang::FieldDecl* fieldDecl : fields)
  {
    auto result =
        alchemy::parser::StructExtractor::extractField(fieldDecl,
                                                       ast->getASTContext(),
                                                       ast->getSourceManager(),
                                                       ast->getLangOpts());

    ASSERT_TRUE(result.valid())
        << "alchemy::testing::unit::pointer field extraction should succeed";

    validatePointerField(result.value());
  }
}

}  // namespace alchemy::testing
