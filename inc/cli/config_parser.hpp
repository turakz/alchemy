#ifndef ALCHEMY_CONFIG_CONFIG_PARSER_HPP
#define ALCHEMY_CONFIG_CONFIG_PARSER_HPP
// std
#include <filesystem>

// local
#include "app/core/core.hpp"
#include "cli/cli.hpp"

namespace alchemy::config::parser {

alchemy::core::Result<alchemy::cli::CliInputs>
loadConfig(const std::filesystem::path& configLoc);

void
dumpConfig(const std::filesystem::path& configDir,
           const alchemy::cli::ParsedOptions& parsedOptions);

}  // namespace alchemy::config::parser

#endif  // ALCHEMY_CONFIG_CONFIG_PARSER_HPP
