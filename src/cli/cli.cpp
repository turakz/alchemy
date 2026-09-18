// src/cli/cli.cpp
#include "cli/cli.hpp"

// std
#include <cstddef>

#include <exception>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

// 3rd party
#include <llvm/Support/CommandLine.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "app/version.hpp"
#include "cli/config_parser.hpp"
#include "logger/logger.hpp"

void
alchemy::cli::Validator::applyDefaults(CliInputs& inputs)
{
  // build config defaults (cross-cutting, apply to all features)
  if (inputs.jobs == 0)
  {
    inputs.jobs = std::thread::hardware_concurrency();
    alchemy::logger::debug("alchemy::cli::Validator::applyDefaults: jobs=0 "
                           "auto-detected to {} cores\n",
                           inputs.jobs);
  }
}

alchemy::core::Result<bool>
alchemy::cli::Validator::validateBasicRequirements(
    const alchemy::cli::FeatureFlags& flags,
    const alchemy::cli::PathOptions& paths)
{
  // error: at least one feature must be enabled
  if (!flags.enableSalign)
  {
    return alchemy::core::Result<bool>::failure(alchemy::core::Error::format(
        "alchemy::cli::Validator",
        "at least one feature must be enabled (try --salign)"));
  }

  // error: source file patterns required
  if (paths.sourcePatterns.empty())
  {
    return alchemy::core::Result<bool>::failure(alchemy::core::Error::format(
        "alchemy::cli::Validator", "source file patterns are required"));
  }

  // warning: salign ignores outputDir if provided
  if (flags.enableSalign && !paths.outputDir.empty())
  {
    alchemy::logger::warn(
        "{}warning{}: --salign mutates files in-place and will ignore "
        "output directory\n",
        alchemy::color::ansi::Yellow,
        alchemy::color::ansi::Reset);
  }

  return alchemy::core::Result<bool>::success(true);
}

alchemy::core::Result<alchemy::cli::ParsedOptions>
alchemy::cli::Validator::validate(alchemy::cli::CliInputs&& inputs)
{
  alchemy::logger::debug(
      "alchemy::cli::Validator::validate: rootDir={}, buildDir={}, "
      "outputDir={}, "
      "sourcePatterns={}, excludePatterns={}, salign={}, jobs={}, "
      "dryRun={}, dumpConfig={}, debug={}\n",
      inputs.paths.rootDir,
      inputs.paths.buildDir,
      inputs.paths.outputDir,
      inputs.paths.sourcePatterns.size(),
      inputs.paths.excludePatterns.size(),
      inputs.features.enableSalign,
      inputs.jobs,
      inputs.enableDryRun,
      inputs.dumpConfig,
      inputs.enableDebug);

  // step 1: apply defaults (including feature-specific defaults)
  alchemy::cli::Validator::applyDefaults(inputs);

  // step 2: validate basic requirements and emit warnings
  auto validation = alchemy::cli::Validator::validateBasicRequirements(
      inputs.features, inputs.paths);
  if (validation.invalid())
  {
    return alchemy::core::Result<alchemy::cli::ParsedOptions>::failure(
        std::move(validation).error());
  }

  // step 3: construct final ParsedOptions
  alchemy::cli::ParsedOptions options;
  options.rootDir = inputs.paths.rootDir;
  options.buildDir = inputs.paths.buildDir;
  options.outputDir = inputs.paths.outputDir;
  options.sourcePatterns = std::move(inputs.paths.sourcePatterns);
  options.excludePatterns = std::move(inputs.paths.excludePatterns);
  options.enableSalign = inputs.features.enableSalign;
  options.jobs = inputs.jobs;
  options.enableDryRun = inputs.enableDryRun;
  options.dumpConfig = inputs.dumpConfig;
  options.enableDebug = inputs.enableDebug;

  return alchemy::core::Result<alchemy::cli::ParsedOptions>::success(
      std::move(options));
}

alchemy::cli::CliInputs
alchemy::cli::mergeInputs(CliInputs&& cli, CliInputs&& config)
{
  alchemy::cli::CliInputs merged = std::move(cli);

  if (merged.paths.buildDir.empty())
  {
    merged.paths.buildDir = std::move(config.paths.buildDir);
  }
  if (merged.paths.outputDir.empty())
  {
    merged.paths.outputDir = std::move(config.paths.outputDir);
  }
  if (merged.paths.sourcePatterns.empty())
  {
    merged.paths.sourcePatterns = std::move(config.paths.sourcePatterns);
  }
  if (merged.paths.excludePatterns.empty())
  {
    merged.paths.excludePatterns = std::move(config.paths.excludePatterns);
  }
  if (!merged.features.enableSalign)
  {
    merged.features.enableSalign = config.features.enableSalign;
  }
  if (!merged.enableDryRun)
  {
    merged.enableDryRun = config.enableDryRun;
  }
  if (merged.jobs == 1 && config.jobs != 1)
  {
    merged.jobs = config.jobs;
  }

  alchemy::logger::debug(
      "alchemy::cli::mergeInputs: effective config: buildDir={}, "
      "sources={} pattern(s), exclude={} pattern(s), "
      "salign={}, jobs={}, dryRun={}\n",
      merged.paths.buildDir,
      merged.paths.sourcePatterns.size(),
      merged.paths.excludePatterns.size(),
      merged.features.enableSalign,
      merged.jobs,
      merged.enableDryRun);

  return merged;
}

// ============================================================================
// CLI parsing functions
// ============================================================================

alchemy::core::Result<alchemy::cli::ParsedOptions>
alchemy::cli::parseCli(int argc, const char** argv)
{
  try
  {
    // static LLVM objects: initialized once, avoiding multiple registry issues
    static llvm::cl::OptionCategory category("alchemy::cli::options");

    static llvm::cl::opt<std::string> buildDirOpt(
        "build-dir",
        llvm::cl::desc("project build directory"),
        llvm::cl::value_desc("file_path"),
        llvm::cl::cat(category));
    static const llvm::cl::alias BuildDirOptAlias(
        "b",
        llvm::cl::desc("alias for --build-dir"),
        llvm::cl::aliasopt(buildDirOpt));

    static llvm::cl::opt<std::string> outputDirOpt(
        "output-dir",
        llvm::cl::desc("output directory"),
        llvm::cl::value_desc("file_path"),
        llvm::cl::cat(category));
    static const llvm::cl::alias OutputDirOptAlias(
        "o",
        llvm::cl::desc("alias for --output-dir"),
        llvm::cl::aliasopt(outputDirOpt));

    static llvm::cl::opt<bool> salignOpt(
        "salign",
        llvm::cl::desc("struct alignment optimization"),
        llvm::cl::cat(category));

    // source file patterns (positional arguments)
    static llvm::cl::list<std::string> sourcePatternsOpt(
        llvm::cl::Positional,
        llvm::cl::desc("<source0> [... sourceN]"),
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(category));

    // source file exclusion patterns (do not process)
    static llvm::cl::list<std::string> excludePatternsOpt(
        "exclude",
        llvm::cl::desc("exclude source patterns"),
        llvm::cl::value_desc("file_path | file_glob"),
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(category));

    static llvm::cl::opt<unsigned> jobsOpt(
        "jobs",
        llvm::cl::desc("number of parallel jobs (default: 1, use 0 for "
                       "auto-detect CPU cores)"),
        llvm::cl::init(1),
        llvm::cl::value_desc("N"),
        llvm::cl::cat(category));
    static const llvm::cl::alias JobsOptAlias(
        "j", llvm::cl::desc("alias for --jobs"), llvm::cl::aliasopt(jobsOpt));

    static llvm::cl::opt<bool> dryRunOpt(
        "dry-run",
        llvm::cl::desc("enable dry run, re-directs output to stdout"),
        llvm::cl::cat(category));

    // dump cmd line args to a config file
    static llvm::cl::opt<bool> dumpConfigOpt(
        "dump-config",
        llvm::cl::desc("write cmd-line args to config file"),
        llvm::cl::cat(category));

    static llvm::cl::opt<bool> debugOpt("debug",
                                        llvm::cl::desc("enable debug logging"),
                                        llvm::cl::cat(category));

    // suppress LLVM/non-related cli options
    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream& os) {
      os << "alchemy v" << alchemy::Version << "\n";
    });
    // parse command line directly (no CommonOptionsParser, avoids premature
    // compilation database auto-detection that produces misleading errors
    // before alchemy's own -b flag and file discovery have run)
    if (!llvm::cl::ParseCommandLineOptions(argc, argv, "alchemy"))
    {
      return alchemy::core::Result<alchemy::cli::ParsedOptions>::failure(
          alchemy::core::Error::format("alchemy::cli",
                                       "failed to parse cmd line arguments"));
    }

    // canonicalize paths only when provided (canonical throws on empty strings)
    auto buildDir = buildDirOpt.getValue();
    auto outputDir = outputDirOpt.getValue();

    // collect CLI inputs (grouped by semantic purpose)
    alchemy::cli::CliInputs inputs{
        .paths =
            {.rootDir = std::filesystem::current_path().string(),
             .buildDir = buildDir.empty()
                             ? ""
                             : std::filesystem::canonical(buildDir).string(),
             .outputDir = outputDir.empty()
                              ? ""
                              : std::filesystem::canonical(outputDir).string(),
             .sourcePatterns = std::vector<std::string>(
                 std::begin(sourcePatternsOpt), std::end(sourcePatternsOpt)),
             .excludePatterns = std::vector<std::string>(
                 std::begin(excludePatternsOpt), std::end(excludePatternsOpt))},
        .features = {.enableSalign = salignOpt.getValue()},
        .jobs = static_cast<std::size_t>(jobsOpt.getValue()),
        .enableDryRun = dryRunOpt.getValue(),
        .dumpConfig = dumpConfigOpt.getValue(),
        .enableDebug = debugOpt.getValue()};

    // parse a config if it exists, cli args take precedence
    const std::filesystem::path ConfigLoc =
        std::filesystem::current_path() / ".alchemy/alchemy.toml";
    if (std::filesystem::exists(ConfigLoc))
    {
      alchemy::logger::info("alchemy::{}cli{}::config::file found: {}{}{}\n",
                            alchemy::color::ansi::BoldBrightGreen,
                            alchemy::color::ansi::Reset,
                            alchemy::color::ansi::BoldBrightGreen,
                            ConfigLoc.string(),
                            alchemy::color::ansi::Reset);
      auto configResult = alchemy::config::parser::loadConfig(ConfigLoc);
      if (configResult.valid())
      {
        alchemy::logger::info("alchemy::{}cli{}::config::loaded {}{}{}\n",
                              alchemy::color::ansi::BoldBrightGreen,
                              alchemy::color::ansi::Reset,
                              alchemy::color::ansi::BrightGreen,
                              ConfigLoc.string(),
                              alchemy::color::ansi::Reset);

        auto configInputs = configResult.value();

        // canonicalize config paths (CLI paths are already canonical)
        if (!configInputs.paths.buildDir.empty())
        {
          configInputs.paths.buildDir =
              std::filesystem::canonical(configInputs.paths.buildDir).string();
        }
        if (!configInputs.paths.outputDir.empty())
        {
          configInputs.paths.outputDir =
              std::filesystem::canonical(configInputs.paths.outputDir).string();
        }

        inputs = alchemy::cli::mergeInputs(std::move(inputs),
                                           std::move(configInputs));
      }
      else
      {
        alchemy::logger::warn(
            "alchemy::{}config{}::{}warning{}: failed to parse "
            "{}: {}\n",
            alchemy::color::ansi::BoldBrightGreen,
            alchemy::color::ansi::Reset,
            alchemy::color::ansi::Yellow,
            alchemy::color::ansi::Reset,
            ConfigLoc.string(),
            configResult.error());
      }
    }

    // validate and construct final options
    return alchemy::cli::Validator::validate(std::move(inputs));
  }
  catch (const std::exception& e)
  {
    return alchemy::core::Result<alchemy::cli::ParsedOptions>::failure(
        alchemy::core::Error::format(
            "alchemy::cli", "parsing failed: {}", e.what()));
  }
}
