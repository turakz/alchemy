#ifndef ALCHEMY_CLI_CLI_HPP
#define ALCHEMY_CLI_CLI_HPP

// std
#include <cstddef>

#include <filesystem>
#include <string>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"

namespace alchemy::cli {

/// path-related options (input/output locations)
struct PathOptions {
  std::string buildDir;
  std::string outputDir;
  std::vector<std::string> sourcePatterns;
  std::vector<std::string> excludePatterns;
};

/// feature flags (which operations to enable)
struct FeatureFlags {
  bool enableSalign{false};
};

/// aggregate of all CLI inputs (before validation)
struct CliInputs {
  PathOptions paths{};
  FeatureFlags features{};
  std::size_t jobs{1};
  bool enableDryRun{false};
  bool dumpConfig{false};
};

struct ParsedOptions {
  std::filesystem::path buildDir{};
  std::filesystem::path outputDir{};
  std::vector<std::string> sourcePatterns;
  std::vector<std::string> excludePatterns;

  bool enableSalign{false};

  std::size_t jobs{1};  // number of threads (0 = auto-detect)

  bool enableDryRun{false};
  bool dumpConfig{false};

  ~ParsedOptions() = default;
  ParsedOptions() = default;
  ParsedOptions(const ParsedOptions&) = delete;
  ParsedOptions&
  operator=(const ParsedOptions&) = delete;
  ParsedOptions(ParsedOptions&&) = default;
  ParsedOptions&
  operator=(ParsedOptions&&) = default;
};

// ============================================================================
// validation: centralized validation logic
// ============================================================================

/// validates and transforms CLI inputs into final ParsedOptions
class Validator {
public:
  /// validate CLI inputs and produce final ParsedOptions
  static alchemy::core::Result<ParsedOptions>
  validate(CliInputs&& inputs);

private:
  /// apply defaults to inputs (e.g., jobs=0 -> hardware_concurrency)
  static void
  applyDefaults(CliInputs& inputs);

  /// validate basic requirements and emit user-friendly warnings
  static alchemy::core::Result<bool>
  validateBasicRequirements(const FeatureFlags& flags,
                            const PathOptions& paths);
};

alchemy::cli::CliInputs
mergeInputs(CliInputs&& cli, CliInputs&& config);

alchemy::core::Result<ParsedOptions>
parseCli(int argc, const char** argv);

}  // namespace alchemy::cli
#endif  // ALCHEMY_CLI_CLI_HPP
