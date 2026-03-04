// src/cli/config_parser.cpp
#include "cli/config_parser.hpp"

// std
#include <cstddef>
#include <cstdint>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

// 3rd party
#include <tomlplusplus/toml.hpp>

// local
#include "app/core/core.hpp"
#include "cli/cli.hpp"

alchemy::core::Result<alchemy::cli::CliInputs>
alchemy::config::parser::loadConfig(const std::filesystem::path& configLoc)
{
  try
  {
    toml::table config = toml::parse_file(configLoc.string());

    alchemy::cli::CliInputs inputs;

    inputs.paths.buildDir =
        config["options"]["build-dir"].value_or<std::string>("");
    inputs.paths.outputDir =
        config["options"]["output-dir"].value_or<std::string>("");

    if (const auto* arr = config["options"]["sources"].as_array())
    {
      for (const auto& elem : *arr)
      {
        if (auto str = elem.value<std::string>())
        {
          inputs.paths.sourcePatterns.push_back(*str);
        }
      }
    }

    if (const auto* arr = config["options"]["exclude"].as_array())
    {
      for (const auto& elem : *arr)
      {
        if (auto str = elem.value<std::string>())
        {
          inputs.paths.excludePatterns.push_back(*str);
        }
      }
    }

    inputs.features.enableSalign = config["options"]["salign"].value_or(false);
    inputs.jobs =
        static_cast<std::size_t>(config["options"]["jobs"].value_or(1));
    inputs.enableDryRun = config["options"]["dry-run"].value_or(false);

    return alchemy::core::Result<alchemy::cli::CliInputs>::success(
        std::move(inputs));
  }
  catch (const toml::parse_error& err)
  {
    return alchemy::core::Result<alchemy::cli::CliInputs>::failure(
        alchemy::core::Error::format("alchemy::config::parser",
                                     "config parse failed: {}",
                                     err.description()));
  }
}

void
alchemy::config::parser::dumpConfig(
    const std::filesystem::path& configDir,
    const alchemy::cli::ParsedOptions& parsedOptions)
{
  toml::table options;
  options.insert("build-dir", parsedOptions.buildDir.string());
  options.insert("output-dir", parsedOptions.outputDir.string());
  options.insert("salign", parsedOptions.enableSalign);

  toml::array sources;
  for (const auto& pattern : parsedOptions.sourcePatterns)
  {
    sources.push_back(pattern);
  }
  options.insert("sources", std::move(sources));

  toml::array exclusions;
  for (const auto& pattern : parsedOptions.excludePatterns)
  {
    exclusions.push_back(pattern);
  }
  options.insert("exclude", std::move(exclusions));

  options.insert("jobs", static_cast<int64_t>(parsedOptions.jobs));
  options.insert("dry-run", parsedOptions.enableDryRun);

  toml::table config;
  config.insert("options", std::move(options));

  std::ofstream configFile(configDir / "alchemy.toml",
                           std::ios::binary | std::ios::trunc);
  configFile << config;  // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                         // toml++ internal
}
