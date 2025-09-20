// 3rd party
#include <clang/AST/AST.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>

// std
#include <iostream>

auto main(void) -> int32_t
{
  std::cout << "LightningStructs::Main::libclang C++ API appears to be usable!" << std::endl;

  // optional: very minimal test to ensure headers are linkable
  clang::ASTContext *ctx = nullptr;
  clang::TranslationUnitDecl *tuDecl = nullptr;

  if (!ctx && !tuDecl)
  {
      std::cout << "LightningStructs::Main::AST headers included successfully." << std::endl;
  }

  return 0;
}
