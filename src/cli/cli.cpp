// src/cli/cli.cpp
// std
#include <exception>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>

// 3rd party

#include "app/color.hpp"
#include "app/core/core.hpp"
#include "clang/Tooling/CommonOptionsParser.h"

// local
#include "cli/cli.hpp"

void
alchemy::cli::Validator::applyDefaults(CliInputs& inputs)
{
  // build config defaults (cross-cutting, apply to all features)
  if (inputs.jobs == 0)
  {
    inputs.jobs = std::thread::hardware_concurrency();
  }
}

alchemy::core::Result<bool>
alchemy::cli::Validator::validateBasicRequirements(const FeatureFlags& flags,
                                                   const PathOptions& paths)
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
    fmt::print(stderr,
               "{}warning{}: --salign mutates files in-place and will ignore "
               "output directory\n",
               alchemy::color::ansi::Yellow,
               alchemy::color::ansi::Reset);
  }

  return alchemy::core::Result<bool>::success(true);
}

alchemy::core::Result<alchemy::cli::ParsedOptions>
alchemy::cli::Validator::validate(CliInputs&& inputs)
{
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
  options.buildDir = inputs.paths.buildDir;
  options.outputDir = inputs.paths.outputDir;
  options.sourcePatterns = std::move(inputs.paths.sourcePatterns);
  options.excludePatterns = std::move(inputs.paths.excludePatterns);
  options.enableSalign = inputs.features.enableSalign;
  options.enableDryRun = inputs.enableDryRun;
  options.jobs = inputs.jobs;

  return alchemy::core::Result<alchemy::cli::ParsedOptions>::success(
      std::move(options));
}

// ============================================================================
// CLI parsing functions
// ============================================================================

alchemy::core::Result<alchemy::cli::ParsedOptions>
alchemy::cli::parseCli(int argc, const char** argv)
{
  try
  {
    // static LLVM objects - initialized once, avoiding multiple registry issues
    static llvm::cl::OptionCategory category("alchemy::cli::options");

    static llvm::cl::opt<std::string> buildDirOpt(
        "b",
        llvm::cl::desc("alchemy::cli::build directory (-p is clang's)"),
        llvm::cl::value_desc("file_path"),
        llvm::cl::cat(category));

    static llvm::cl::opt<std::string> outputDirOpt(
        "o",
        llvm::cl::desc("alchemy::cli::output directory"),
        llvm::cl::value_desc("file_path"),
        llvm::cl::cat(category));

    static llvm::cl::opt<bool> salignOpt(
        "salign",
        llvm::cl::desc("alchemy::cli::enable struct alignment optimization"),
        llvm::cl::cat(category));

    static llvm::cl::opt<bool> dryRunOpt(
        "dry-run",
        llvm::cl::desc(
            "alchemy::cli::enable dry run, re-directs output to stdout"),
        llvm::cl::cat(category));

    static llvm::cl::list<std::string> excludeOpt(
        "exclude",
        llvm::cl::desc("alchemy::cli::exclude source patterns"),
        llvm::cl::value_desc("file_path | file_glob"),
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(category));

    static llvm::cl::opt<unsigned> jobsOpt(
        "j",
        llvm::cl::desc("Number of parallel jobs (default: 1, use 0 for "
                       "auto-detect based on CPU cores"),
        llvm::cl::init(1),
        llvm::cl::value_desc("N"),
        llvm::cl::cat(category));

    // let LLVM parse the command line
    auto llvmParser =
        clang::tooling::CommonOptionsParser::create(argc, argv, category);
    if (!llvmParser)
    {
      std::string error = llvm::toString(llvmParser.takeError());

      return alchemy::core::Result<alchemy::cli::ParsedOptions>::failure(
          alchemy::core::Error::format(
              "alchemy::cli", "failed to parse cmd line arguments: {}", error));
    }

    // collect CLI inputs (grouped by semantic purpose)
    CliInputs inputs{
        .paths = {.buildDir = buildDirOpt.getValue(),
                  .outputDir = outputDirOpt.getValue(),
                  .sourcePatterns = llvmParser->getSourcePathList(),
                  .excludePatterns = std::vector<std::string>(
                      excludeOpt.begin(), excludeOpt.end())},
        .features = {.enableSalign = salignOpt.getValue()},
        .jobs = static_cast<std::size_t>(jobsOpt.getValue()),
        .enableDryRun = dryRunOpt.getValue()};

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
