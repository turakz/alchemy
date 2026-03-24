#ifndef ALCHEMY_CONFIG_CONFIG_HPP
#define ALCHEMY_CONFIG_CONFIG_HPP
// std
#include <filesystem>
#include <vector>

// 3rd party

// local
#include "cli/cli.hpp"

namespace alchemy::config {

// source file inventory (discovered files)
struct SourceInventory {
  std::vector<std::filesystem::path> sourceFiles{};
  std::vector<std::filesystem::path> excludedFiles{};
};

// application configuration: validated CLI options and discovered source files
struct AppConfig {
  alchemy::cli::ParsedOptions cliArgs{};
  SourceInventory inventory{};
  std::filesystem::path rootDir;
};

}  // namespace alchemy::config
#endif  // ALCHEMY_CONFIG_CONFIG_HPP
