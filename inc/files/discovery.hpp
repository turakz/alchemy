#ifndef ALCHEMY_FILES_DISCOVERY_HPP
#define ALCHEMY_FILES_DISCOVERY_HPP
// std
#include <cstddef>

#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"

namespace alchemy::discovery {
namespace detail {
std::filesystem::path
findCommonAncestor(const std::vector<std::filesystem::path>& paths);
}

struct DiscoveryResult {
  std::vector<std::filesystem::path> sourceFiles;
  std::vector<std::filesystem::path> excludedFiles;
};

// discovery configuration derived from patterns
struct DiscoveryConfig {
  std::filesystem::path searchRoot;
  std::unordered_set<std::string> targetExtensions;
  bool needsRecursiveSearch{false};
};

// compiled pattern for optimized matching
struct CompiledPattern {
  std::string original;      // original pattern string
  std::regex compiledRegex;  // pre-compiled regex
};

// phase 1: build discovery configuration from patterns
DiscoveryConfig
buildDiscoveryConfig(const std::vector<std::string>& includePatterns,
                     const std::vector<std::string>& excludePatterns = {});

// phase 2: collect all candidate files with target extensions
std::vector<std::filesystem::path>
findCandidateFiles(const DiscoveryConfig& config);

// phase 3: optimized pattern matching
// pre-compile patterns (avoids recompiling regex per file)
std::string
globToRegex(std::string_view pattern);

std::vector<CompiledPattern>
compilePatterns(const std::vector<std::string>& patterns);

bool
matchesPattern(const std::filesystem::path& file,
               const CompiledPattern& pattern);

// main entry point
alchemy::core::Result<DiscoveryResult>
discoverFiles(const std::vector<std::string>& sourcePatterns,
              const std::vector<std::string>& excludePatterns = {},
              std::size_t jobs = 1);

}  // namespace alchemy::discovery
#endif  // ALCHEMY_FILES_DISCOVERY_HPP
