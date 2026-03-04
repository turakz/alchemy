// src/app/app.cpp
// std
#include <cstdint>
#include <cstdlib>

#include <exception>
#include <memory>
#include <utility>
#include <vector>

// 3rd party
#include "app/core/core.hpp"
#include "cli/cli.hpp"
#include "fmt/core.h"

// local
#include "app/app.hpp"
#include "app/color.hpp"
#include "config/config.hpp"
#include "files/discovery.hpp"
#include "operation/operation.hpp"
#include "parsing/libclang/clang_parser.hpp"
#include "parsing/parser.hpp"
#include "pipeline/pipeline.hpp"
#include "reporting/metrics_reporter.hpp"

alchemy::core::Result<alchemy::App>
alchemy::App::create(alchemy::cli::ParsedOptions options)
{
  try
  {
    if (!std::filesystem::exists(options.buildDir))
    {
      return alchemy::core::Result<alchemy::App>::failure(
          alchemy::core::Error::format("alchemy::App::create",
                                       "build dir does not exist for: {}",
                                       options.buildDir.string()));
    }
    fmt::print("alchemy::{}transmuting{}...\n",
               alchemy::color::ansi::BrightGreen,
               alchemy::color::ansi::Reset);
    auto discoveryResult = alchemy::discovery::discoverFiles(
        options.sourcePatterns, options.excludePatterns, options.jobs);

    if (discoveryResult.invalid())
    {
      return alchemy::core::Result<alchemy::App>::failure(
          alchemy::core::Error::format("alchemy::App::create",
                                       "file discovery failed: {}",
                                       std::move(discoveryResult).error()));
    }

    alchemy::config::AppConfig context;
    context.cliArgs = std::move(options);
    auto discoveryValue = std::move(discoveryResult).value();
    context.inventory.sourceFiles = std::move(discoveryValue.sourceFiles);
    context.inventory.excludedFiles = std::move(discoveryValue.excludedFiles);

    return alchemy::core::Result<alchemy::App>::success(
        App{std::move(context)});
  }
  catch (const std::exception& e)
  {
    return alchemy::core::Result<App>::failure(alchemy::core::Error::format(
        "alchemy::App::create", "exception: {}", e.what()));
  }
}

alchemy::core::Result<alchemy::App>
alchemy::App::createFromCli(int argc, const char** argv)
{
  auto parseResult = alchemy::cli::parseCli(argc, argv);
  if (parseResult.invalid())
  {
    return alchemy::core::Result<App>::failure(
        alchemy::core::Error::format("alchemy::App::createFromCli",
                                     "cli parsing failed: {}",
                                     std::move(parseResult).error()));
  }

  return alchemy::App::create(std::move(parseResult).value());
}

// app-specific helper functions
std::vector<alchemy::operation::RecipeOperation>
alchemy::App::createRecipeOperations(const alchemy::config::AppConfig& context)
{
  std::vector<alchemy::operation::RecipeOperation> operations;

  if (context.cliArgs.enableSalign)
  {
    operations.emplace_back(
        alchemy::operation::refactoring::StructAlignmentOperation());
  }

  return operations;
}

std::int32_t
alchemy::App::exec()
{
  if (context().inventory.sourceFiles.empty())
  {
    fmt::print("alchemy::{}no source files to process{}\n",
               alchemy::color::ansi::Magenta,
               alchemy::color::ansi::Reset);
    return EXIT_SUCCESS;
  }

  try
  {
    // create parser backend
    std::unique_ptr<alchemy::parser::ParsingRuleAdapter> parser;

    alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>
        clangResult = alchemy::parser::ClangParser::create(
            alchemy::App::context().inventory.sourceFiles,
            alchemy::App::context().cliArgs.buildDir);

    if (clangResult.invalid())
    {
      fmt::print(
          "{}",
          alchemy::core::Error::format("alchemy::App::exec",
                                       "failed to create clang parser: {}",
                                       std::move(clangResult).error()));
      return EXIT_FAILURE;
    }
    parser = std::move(clangResult).value();

    // create recipe operations based on enabled features
    const std::vector<alchemy::operation::RecipeOperation> Operations =
        alchemy::App::createRecipeOperations(alchemy::App::context());

    // execute full pipeline: parse → execute → transmute
    alchemy::core::Result<alchemy::pipeline::PipelineResult> pipelineResult =
        alchemy::pipeline::execute<alchemy::operation::RecipeOperation>(
            *parser,
            Operations,
            alchemy::App::context().cliArgs.buildDir,
            alchemy::App::context().cliArgs.enableDryRun);

    if (pipelineResult.invalid())
    {
      fmt::print(
          "{}",
          alchemy::core::Error::format("alchemy::App::exec",
                                       "pipeline execution failed: {}",
                                       std::move(pipelineResult).error()));
      return EXIT_FAILURE;
    }

    // report transmutation summary
    const auto& summary = pipelineResult.value().summary;
    fmt::print(
        "alchemy::{}summary{}::{}{}{} recipes applied across {}{}{} files\n",
        alchemy::color::ansi::BrightGreen,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Cyan,
        summary.recipesApplied,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Cyan,
        summary.filesProcessed,
        alchemy::color::ansi::Reset);

    // report metrics (moved from pipeline layer for separation of concerns)
    if (!pipelineResult.value().allMetrics.empty())
    {
      alchemy::metrics::reporter::reportMetrics(
          pipelineResult.value().allMetrics);
    }
    else
    {
      fmt::print(stdout,
                 "alchemy::App::{}reporting{}::{}no metrics produced{}\n",
                 alchemy::color::ansi::BrightGreen,
                 alchemy::color::ansi::Reset,
                 alchemy::color::ansi::Magenta,
                 alchemy::color::ansi::Reset);
    }

    return EXIT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    fmt::print("{}",
               alchemy::core::Error::format(
                   "alchemy::App::exec", "caught exception: {}", e.what()));
    return EXIT_FAILURE;
  }
}
