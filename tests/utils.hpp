#ifndef ALCHEMY_TESTS_UTILS_HPP
#define ALCHEMY_TESTS_UTILS_HPP

// std
#include <filesystem>
#include <string>
#include <vector>

// local
#include "app/app.hpp"
#include "app/core/core.hpp"
#include "cli/cli.hpp"
#include "operation/operation.hpp"

namespace alchemy::testing::utils {

// file creation utilities

// create a test file with given content in specified directory
std::filesystem::path
createTestFile(const std::filesystem::path& directory,
               const std::string& filename,
               const std::string& content = "");

/// read entire file contents as string
std::string
readFile(const std::filesystem::path& filePath);

// create a synthetic compilation database for unit testing
// NOTE: This creates unrealistic databases with header entries.
// -> strictly for quick unit testing APIs
void
createSyntheticCompilationDatabase(
    const std::filesystem::path& directory,
    const std::vector<std::string>& sourceFiles = {});

// create a realistic GCC compilation database with absolute paths
void
createGCCDatabase(const std::filesystem::path& buildDir,
                  const std::filesystem::path& projectRoot);

// create a realistic Clang compilation database with absolute paths
void
createClangDatabase(const std::filesystem::path& buildDir,
                    const std::filesystem::path& projectRoot);

// create a realistic IAR compilation database with absolute paths
void
createIARDatabase(const std::filesystem::path& buildDir,
                  const std::filesystem::path& projectRoot);

// create a realistic MSVC compilation database with absolute paths
void
createMSVCDatabase(const std::filesystem::path& buildDir,
                   const std::filesystem::path& projectRoot);

// copy directory recursively
void
copyDirectory(const std::filesystem::path& src,
              const std::filesystem::path& dst);

// recipe creation utilities

// create a refactor recipe for transmutation testing
alchemy::operation::Recipe
createRecipe(const std::filesystem::path& sourceFile,
             std::size_t byteOffset,
             std::size_t byteLength,
             const std::string& replacementText);

// parser testing utilities

// create a fully-initialized StructDef for testing (mimics parse-time initialization)
// NOTE: This calls production methods (computeSize, computeDataSize) to ensure
// test data is consistent with actual parsing behavior.
alchemy::parser::artifacts::StructDef
createStructDef(
    const std::string& structName,
    const std::filesystem::path& sourceFile,
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
    std::size_t naturalAlignment);

// create a StructDef with explicit hand-calculated layout values
// Use this when testing code that reads these values to avoid circular
// verification (where the helper and the code under test use the same methods).
alchemy::parser::artifacts::StructDef
createStructDefExplicit(
    const std::string& structName,
    const std::filesystem::path& sourceFile,
    const std::vector<alchemy::parser::artifacts::FieldDef>& fields,
    std::size_t naturalAlignment,
    std::size_t currentDataSize,
    std::size_t naturalTotalSize,
    std::size_t currentWastedBytes);

// create a FieldDef for testing
alchemy::parser::artifacts::FieldDef
createFieldDef(unsigned byteOffset,
               unsigned byteLength,
               const std::string& typeName,
               const std::string& fieldName,
               std::size_t naturalSize,
               std::size_t naturalAlignment,
               bool isBitField = false,
               bool canReorder = true);

// struct layout calculation utilities (platform-independent)

// CLI mocking utilities

// mock build configuration for testing (avoids LLVM global state, easier to
// extend)
struct MockCliConfig {
  std::filesystem::path buildDir;
  std::filesystem::path outputDir;
  std::vector<std::string> sourceFiles = {};
  std::vector<std::string> excludePatterns = {};
  bool enableSalign = false;
  bool enableDryRun = false;
  std::size_t jobs = 1;
};

// create CliInputs from config struct (for Validator testing)
alchemy::cli::CliInputs
createCliInputs(const MockCliConfig& config);

// create mock CLI options from config struct (bypasses validation)
alchemy::cli::ParsedOptions
createMockOptions(const MockCliConfig& config);

// create alchemy app with mock config (convenience wrapper)
alchemy::core::Result<alchemy::App>
createAlchemyWithMockOptions(const MockCliConfig& config);

}  // namespace alchemy::testing::utils
#endif  // ALCHEMY_TESTS_UTILS_HPP
