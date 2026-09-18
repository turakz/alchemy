// src/clang_parser.cpp
#include "parsing/libclang/clang_parser.hpp"

// std
#include <chrono>
#include <cstddef>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 3rd party
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "files/discovery.hpp"
#include "logger/logger.hpp"
#include "parsing/artifacts/artifacts.hpp"
#include "parsing/libclang/clang_struct_parsing_rule.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_adapter.hpp"
#include "parsing/libclang/compiler_adapters/clang_compilation_database_factory.hpp"
#include "parsing/libclang/compiler_adapters/compiler_utils.hpp"
#include "parsing/parsing_requirements.hpp"

alchemy::core::Result<
    alchemy::parser::libclang::adapters::CompilationDatabaseInfo>
alchemy::parser::ClangParser::loadCompilationDatabase(
    const std::filesystem::path& buildDir)
{
  alchemy::logger::debug("alchemy::parser::ClangParser::"
                         "loadCompilationDatabase: loading from {}\n",
                         (buildDir / "compile_commands.json").string());

  auto dbResult = alchemy::parser::libclang::adapters::
      CompilationDatabaseFactory::fromBuildDir(buildDir);
  if (dbResult.invalid())
  {
    return alchemy::core::Result<
        alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
        failure(alchemy::core::Error::format(
            "alchemy::parser::ClangParser",
            "failed to load compilation database\n"
            "expected: {}\n"
            "error: {}\n"
            "  note: without a compilation database, alchemy cannot "
            "resolve symbols and will produce incorrect results",
            (buildDir / "compile_commands.json").string(),
            dbResult.error()));
  }
  return alchemy::core::Result<
      alchemy::parser::libclang::adapters::CompilationDatabaseInfo>::
      success(std::move(dbResult).value());
}

void
alchemy::parser::ClangParser::buildParseGraph(
    const clang::tooling::CompileCommand& cmd,
    const std::string& clangBinary,
    ReverseDependencyMap& includedBy)
{
  if (cmd.CommandLine.empty())
  {
    return;
  }

  // static const: initialized once, thread-safe under C++11
  static const std::unordered_set<std::string> FlagsToStrip{
      "-c",
      "-MD",
      "-MMD",
      "-fsyntax-only"};  // injected by translators; irrelevant for -MM
  static const std::unordered_set<std::string> FlagsWithArgToStrip{
      "-o", "-MF", "-MT"};

  // build the -MM argument list: skip index 0 (compiler binary),
  // strip compilation/output flags, inject -MM
  std::vector<std::string> mmArgs;
  mmArgs.reserve(cmd.CommandLine.size());
  for (std::size_t argIdx = 1; argIdx < cmd.CommandLine.size(); ++argIdx)
  {
    const auto& arg = cmd.CommandLine[argIdx];
    if (FlagsToStrip.contains(arg))
    {
      continue;
    }
    if (FlagsWithArgToStrip.contains(arg))
    {
      ++argIdx;  // skip the flag's argument
      continue;
    }
    mmArgs.push_back(arg);
  }

  auto insertIt =
      alchemy::parser::libclang::adapters::findArgInsertionPoint(mmArgs);
  mmArgs.insert(insertIt, "-MM");

  // non-zero exit means the TU couldn't be preprocessed (missing headers,
  // bad flags, etc.) → skip cleanly so its headers fall through to the
  // direct parse fallback.
  static constexpr auto Context =
      "alchemy::parser::ClangParser::buildReverseDependencyMap";
  auto result = alchemy::parser::libclang::adapters::executeCompilerCommand(
      clangBinary, mmArgs, Context, /*allowNonZeroExit=*/false);

  if (result.invalid())
  {
    alchemy::logger::debug(
        "alchemy::parser::ClangParser::buildReverseDependencyMap: "
        "clang -MM skipped for '{}': {}",
        cmd.Filename,
        result.error());
    return;
  }

  // parse Makefile-format output:
  //   target.o: /path/to/source.c \
  //    /path/to/header1.h \
  //    /path/to/header2.h
  const auto& output = result.value();
  auto colonPos = output.find(':');
  if (colonPos == std::string::npos)
  {
    return;
  }

  // tokenize the right-hand side, drop continuation backslashes
  std::istringstream includes(output.substr(colonPos + 1));
  std::string include;
  bool skipFirst = true;  // first token is the TU source file itself
  while (includes >> include)
  {
    if (include == "\\")
    {
      continue;
    }
    if (skipFirst)
    {
      skipFirst = false;
      continue;
    }
    includedBy[include].emplace_back(TranslationUnit{cmd.Filename, cmd});
  }
}

alchemy::parser::ReverseDependencyMap
alchemy::parser::ClangParser::buildReverseDependencyMap(
    const std::vector<clang::tooling::CompileCommand>& allCommands,
    const std::string& clangBinary)
{
  if (allCommands.empty())
  {
    return {};
  }

  alchemy::logger::info(
      "alchemy::{}parser{}::libclang::building dependency graph for {}{}{} "
      "command(s)...\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      alchemy::color::ansi::BrightGreen,
      allCommands.size(),
      alchemy::color::ansi::Reset);

  const std::size_t HwThreads =
      std::max(1U, std::thread::hardware_concurrency());
  const std::size_t NumThreads = std::min(HwThreads, allCommands.size());
  const std::size_t ChunkSz =
      (allCommands.size() + NumThreads - 1) / NumThreads;

  // each thread writes exclusively to its own partial map — no locks needed
  std::vector<ReverseDependencyMap> partialMaps(NumThreads);
  std::vector<std::thread> workers;
  workers.reserve(NumThreads);

  for (std::size_t tIdx = 0; tIdx < NumThreads; ++tIdx)
  {
    workers.emplace_back([&, tIdx]() {
      const std::size_t Start = tIdx * ChunkSz;
      const std::size_t End = std::min(Start + ChunkSz, allCommands.size());
      auto& includedBy = partialMaps[tIdx];
      for (std::size_t i = Start; i < End; ++i)
      {
        buildParseGraph(allCommands[i], clangBinary, includedBy);
      }
    });
  }

  std::ranges::for_each(workers, [](auto& worker) { worker.join(); });

  // merge partial maps into final result (single-threaded, post-join)
  ReverseDependencyMap reverseDepMap;
  for (auto& partial : partialMaps)
  {
    for (auto& [header, entries] : partial)
    {
      auto& target = reverseDepMap[header];
      target.insert(target.end(),
                    std::make_move_iterator(entries.begin()),
                    std::make_move_iterator(entries.end()));
    }
  }

  alchemy::logger::debug(
      "alchemy::parser::ClangParser::buildReverseDependencyMap: "
      "processed {} commands, {} unique header entries in dep map\n",
      allCommands.size(),
      reverseDepMap.size());

  return reverseDepMap;
}

std::vector<std::string>
alchemy::parser::ClangParser::resolveStdPreamble(
    const std::unordered_set<std::string>& headerFiles,
    const std::vector<std::string>& includePaths)
{
  // resolve std-type preamble for directly-parsed headers
  // -> these headers are parsed via inferMissingCompileCommands
  // and may use std types without #including std headers
  std::vector<std::string> preamble;
  if (headerFiles.empty())
  {
    return preamble;
  }

  static constexpr std::array<const char*, 4> StdHeaders = {
      "stdbool.h", "stddef.h", "stdint.h", "time.h"};
  std::vector<std::string> resolved;
  for (const auto* header : StdHeaders)
  {
    for (const auto& dir : includePaths)
    {
      auto path = std::filesystem::path(dir) / header;
      if (std::filesystem::exists(path))
      {
        preamble.emplace_back("-include");
        preamble.emplace_back(path.string());
        resolved.emplace_back(path.string());
        break;
      }
    }
  }

  return preamble;
}

void
alchemy::parser::ClangParser::injectIncludePaths(
    clang::tooling::ClangTool& tool,
    const std::vector<std::string>& dbFiles,
    std::vector<std::string> allIncludes)
{
  // augment inferred commands with project-wide include paths
  // -> headers not in the database get commands via LLVM's interpolation,
  // but the proxy file's -I flags may not cover all includes
  // -> inject the union of all -I paths for inferred files
  if (allIncludes.empty())
  {
    return;
  }

  std::unordered_set<std::string> knownFiles(std::begin(dbFiles),
                                             std::end(dbFiles));

  tool.appendArgumentsAdjuster(
      [knownFiles = std::move(knownFiles),
       allIncludes = std::move(allIncludes)](
          const clang::tooling::CommandLineArguments& args,
          llvm::StringRef filename) -> clang::tooling::CommandLineArguments {
        if (knownFiles.contains(filename.str()))
        {
          return args;
        }

        auto missingFlags =
            alchemy::parser::libclang::adapters::inferMissingIncludeFlags(
                args, allIncludes);

        if (missingFlags.empty())
        {
          return args;
        }

        auto augmented = args;
        auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(
            augmented);
        for (auto& flag : missingFlags)
        {
          it = std::next(augmented.insert(it, std::move(flag)));
        }

        return augmented;
      });
}

void
alchemy::parser::ClangParser::injectStdPreamble(
    clang::tooling::ClangTool& tool,
    std::unordered_set<std::string> headerFiles,
    std::vector<std::string> preamble)
{
  // inject std-type preamble for header files:
  // headers parsed directly (via inferMissingCompileCommands) may use
  // std types (size_t, uint8_t, bool) without #including the std headers.
  if (preamble.empty())
  {
    return;
  }

  tool.appendArgumentsAdjuster(
      [headerFiles = std::move(headerFiles), preamble = std::move(preamble)](
          const clang::tooling::CommandLineArguments& args,
          llvm::StringRef filename) -> clang::tooling::CommandLineArguments {
        if (!headerFiles.contains(filename.str()))
        {
          return args;
        }

        auto augmented = args;
        auto it = alchemy::parser::libclang::adapters::findArgInsertionPoint(
            augmented);

        for (const auto& flag : preamble)
        {
          it = std::next(augmented.insert(it, flag));
        }

        return augmented;
      });
}

void
alchemy::parser::ClangParser::deduplicateStructs(
    std::vector<alchemy::parser::artifacts::StructDef>& structs)
{
  const auto PreDedupCount = structs.size();
  std::unordered_set<std::string> seen;
  std::erase_if(structs, [&seen](const auto& s) {
    auto key = s.sourceFile + "::" + s.structName;
    return !seen.insert(key).second;
  });
  if (PreDedupCount != structs.size())
  {
    alchemy::logger::debug("alchemy::parser::ClangParser::deduplicateStructs: "
                           "removed {} duplicate structs, {} remain\n",
                           PreDedupCount - structs.size(),
                           structs.size());
  }
}

alchemy::core::Result<std::vector<alchemy::parser::ParseCommand>>
alchemy::parser::ClangParser::resolveParseCommands(
    const std::vector<std::filesystem::path>& sourceFiles,
    const std::vector<clang::tooling::CompileCommand>& allCommands,
    const ReverseDependencyMap& reverseDepMap,
    std::unordered_set<std::string>& outTargetHeaders,
    std::vector<std::string>& outDirectHeaders)
{
  using alchemy::parser::libclang::adapters::extractBuildTarget;

  // fileIndex: absPath → [CompileCommand] (exact source file lookup)
  std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>>
      fileIndex;
  for (const auto& cmd : allCommands)
  {
    fileIndex[cmd.Filename].push_back(cmd);
  }

  static const std::unordered_set<std::string> SourceExtensions{
      ".c", ".cpp", ".cc", ".cxx", ".c++"};
  static const std::unordered_set<std::string> HeaderExtensions{
      ".h", ".hh", ".hpp", ".hxx"};

  std::vector<ParseCommand> parseCommands;

  for (const auto& file : sourceFiles)
  {
    auto absPath = std::filesystem::absolute(file).lexically_normal().string();
    outTargetHeaders.insert(absPath);

    auto ext = file.extension().string();

    if (SourceExtensions.contains(ext))
    {
      // source file: emit one spec per compile command (one per target)
      auto it = fileIndex.find(absPath);
      if (it != fileIndex.end())
      {
        for (const auto& cmd : it->second)
        {
          parseCommands.push_back(
              {absPath, extractBuildTarget(cmd.Output), cmd});
        }
      }
      else
      {
        alchemy::logger::debug(
            "alchemy::parser::ClangParser::resolveParseCommands: "
            "source file '{}' not in compilation database, using direct "
            "parse fallback\n",
            absPath);
        outDirectHeaders.push_back(absPath);
      }
      continue;
    }

    if (!HeaderExtensions.contains(ext))
    {
      alchemy::logger::debug(
          "alchemy::parser::ClangParser::resolveParseCommands: "
          "skipping file with unrecognized extension: {}\n",
          absPath);
      continue;
    }

    // header: look up in dep graph (exact reverse dep map from clang -MM)
    auto depIt = reverseDepMap.find(absPath);
    if (depIt == reverseDepMap.end() || depIt->second.empty())
    {
      // no TU compiled this header → direct parse fallback
      outDirectHeaders.push_back(absPath);
      continue;
    }

    // emit one spec per dep graph entry (TU that includes this header).
    // multiple entries = shared header; all are emitted and dedup collapses
    // identical struct output via m_targetHeaders filter.
    for (const auto& entry : depIt->second)
    {
      parseCommands.push_back({entry.tuPath,
                               extractBuildTarget(entry.command.Output),
                               entry.command});
    }
  }

  // deduplicate parseCommands: one per (parseFile, targetName).
  // multiple user headers may resolve to the same TU in the same target;
  // one ClangTool run on that TU captures all of them via m_targetHeaders.
  {
    std::unordered_set<std::string> seen;
    std::erase_if(parseCommands, [&seen](const auto& s) {
      auto key = s.parseFile + "::" + s.targetName;
      return !seen.insert(key).second;
    });
  }

  alchemy::logger::debug("alchemy::parser::ClangParser::resolveParseCommands: "
                         "{} unique parseCommands, {} direct fallback\n",
                         parseCommands.size(),
                         outDirectHeaders.size());

  return alchemy::core::Result<std::vector<alchemy::parser::ParseCommand>>::
      success(std::move(parseCommands));
}

std::vector<clang::tooling::CompileCommand>
alchemy::parser::ClangParser::filterExcludedCommands(
    const std::vector<clang::tooling::CompileCommand>& allCommands,
    const std::vector<std::string>& excludePatterns)
{
  if (excludePatterns.empty())
  {
    return allCommands;
  }

  auto compiled = alchemy::discovery::compilePatterns(excludePatterns);
  std::vector<clang::tooling::CompileCommand> filtered;
  std::size_t excluded = 0;

  for (const auto& cmd : allCommands)
  {
    const bool IsExcluded = std::any_of(
        compiled.begin(),
        compiled.end(),
        [&cmd](const alchemy::discovery::CompiledPattern& p) {
          return alchemy::discovery::matchesPattern(cmd.Filename, p);
        });
    if (IsExcluded)
    {
      ++excluded;
    }
    else
    {
      filtered.push_back(cmd);
    }
  }

  if (excluded > 0)
  {
    alchemy::logger::debug(
        "alchemy::parser::ClangParser::filterExcludedCommands: "
        "excluded {}/{} compile command(s) matching exclude patterns\n",
        excluded,
        allCommands.size());
  }

  return filtered;
}

alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>
alchemy::parser::ClangParser::create(
    const std::vector<std::filesystem::path>& sourceFiles,
    const std::filesystem::path& buildDir,
    const std::vector<std::string>& excludePatterns)
{
  if (sourceFiles.empty())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "no source files to transmute\n"
                "  hint: double check your include or excludes"));
  }

  auto dbResult = loadCompilationDatabase(buildDir);
  if (dbResult.invalid())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            std::move(dbResult).error());
  }
  auto dbInfo = std::move(dbResult).value();
  auto allCommands = dbInfo.database->getAllCompileCommands();

  // filter compile commands whose source file matches an exclude pattern:
  // if the user excluded a path (e.g. **/PSOC6/**), TUs from that path
  // should not contribute to the dep graph or produce parse commands.
  // this prevents cross-target SDK include contamination from ever reaching
  // ClangTool, rather than recovering after the fact.
  auto filteredCommands = filterExcludedCommands(allCommands, excludePatterns);

  // find the host clang binary used to run clang -MM for dep graph construction
  auto clangBinResult = alchemy::parser::libclang::adapters::findClangBinary();
  if (clangBinResult.invalid())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            std::move(clangBinResult).error());
  }
  auto reverseDepMap =
      buildReverseDependencyMap(filteredCommands, clangBinResult.value());

  std::unordered_set<std::string> targetHeaders;
  std::vector<std::string> directHeaders;
  auto parseCommandsResult = resolveParseCommands(sourceFiles,
                                                  filteredCommands,
                                                  reverseDepMap,
                                                  targetHeaders,
                                                  directHeaders);
  if (parseCommandsResult.invalid())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            std::move(parseCommandsResult).error());
  }
  auto parseCommands = std::move(parseCommandsResult).value();

  if (parseCommands.empty() && directHeaders.empty())
  {
    return alchemy::core::
        Result<std::unique_ptr<alchemy::parser::ClangParser>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "no source files matched the compilation database\n"
                "  hint: double check your include or excludes"));
  }

  // build global fallback DB for standalone headers
  auto allCommandsForFallback = filteredCommands;  // copy for inference wrapper
  auto fallbackAdapter = std::make_unique<
      alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter>(
      std::move(allCommandsForFallback));
  std::unique_ptr<clang::tooling::CompilationDatabase> fallbackDb =
      clang::tooling::inferMissingCompileCommands(std::move(fallbackAdapter));

  auto globalIncludes =
      alchemy::parser::libclang::adapters::extractIncludePaths(
          filteredCommands);

  alchemy::logger::info("alchemy::{}parser{}::{}{}{} spec(s) resolved\n",
                        alchemy::color::ansi::BoldBrightGreen,
                        alchemy::color::ansi::Reset,
                        alchemy::color::ansi::BrightGreen,
                        parseCommands.size(),
                        alchemy::color::ansi::Reset);

  alchemy::logger::debug(
      "alchemy::parser::ClangParser::create: {} target header(s) in filter "
      "set, {} direct header(s)\n",
      targetHeaders.size(),
      directHeaders.size());

  auto parser =
      std::make_unique<alchemy::parser::ClangParser>(std::move(parseCommands),
                                                     std::move(directHeaders),
                                                     std::move(fallbackDb),
                                                     std::move(globalIncludes),
                                                     dbInfo.compilerType,
                                                     std::move(targetHeaders));

  return alchemy::core::Result<std::unique_ptr<alchemy::parser::ClangParser>>::
      success(std::move(parser));
}

alchemy::core::Result<std::vector<alchemy::parser::artifacts::StructDef>>
alchemy::parser::ClangParser::run(const ParseCommand& spec) const
{
  // thin single-command DB: exactly the one command for this TU.
  // -> normal compilation, no proxy needed
  auto thinAdapter = std::make_unique<
      alchemy::parser::libclang::adapters::ClangCompilationDatabaseAdapter>(
      std::vector<clang::tooling::CompileCommand>{spec.command});

  clang::tooling::ClangTool tool(*thinAdapter, {spec.parseFile});

  auto structParser = std::make_unique<ClangStructParsingRule>(m_targetHeaders);
  clang::ast_matchers::MatchFinder finder;
  structParser->registerMatchers(finder);

  int clangResult =
      tool.run(clang::tooling::newFrontendActionFactory(&finder).get());

  if (clangResult != 0)
  {
    return alchemy::core::
        Result<std::vector<alchemy::parser::artifacts::StructDef>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "target '{}' [{}]: reported fatal errors during parsing "
                "(exit code: {})\n"
                "  hint: files with errors above may need to be added to "
                "the exclude list in .alchemy/alchemy.toml\n"
                "  note: parsing cannot continue with fatal errors, "
                "type information would be incorrect\n",
                spec.targetName,
                spec.parseFile,
                clangResult));
  }

  if (structParser->hasParseErrors())
  {
    const auto& errors = structParser->getParseErrors();
    std::string errorMsg = std::accumulate(
        std::next(std::begin(errors)),
        std::end(errors),
        std::string("parsing failed with errors: ") + errors.front(),
        [](const std::string& acc, const std::string& err) {
          return acc + "; " + err;
        });
    return alchemy::core::
        Result<std::vector<alchemy::parser::artifacts::StructDef>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser", "{}", errorMsg));
  }

  auto structs = structParser->getParsedStructs();
  for (auto& s : structs)
  {
    s.targetName = spec.targetName;
  }

  alchemy::logger::debug("alchemy::parser::ClangParser::run: "
                         "target '{}' [{}] extracted {} structs\n",
                         spec.targetName,
                         spec.parseFile,
                         structs.size());

  return alchemy::core::
      Result<std::vector<alchemy::parser::artifacts::StructDef>>::success(
          std::move(structs));
}

alchemy::core::Result<std::vector<alchemy::parser::artifacts::StructDef>>
alchemy::parser::ClangParser::run(const std::string& header) const
{
  // standalone header: no TU was found that includes it.
  // use the global infer fallback DB
  // -> LLVM infers flags from the nearest source file,
  // augmented with the union of all project include paths.
  clang::tooling::ClangTool tool(*m_fallbackDb, {header});

  // inject global include paths so the inferred command can find all headers
  injectIncludePaths(tool, {}, m_globalIncludes);

  // inject std preamble so the header can use size_t, uint8_t, bool etc.
  auto preamble = resolveStdPreamble({header}, m_globalIncludes);
  injectStdPreamble(tool, {header}, std::move(preamble));

  auto structParser = std::make_unique<ClangStructParsingRule>(m_targetHeaders);
  clang::ast_matchers::MatchFinder finder;
  structParser->registerMatchers(finder);

  const auto ParseStart = std::chrono::steady_clock::now();
  int clangResult =
      tool.run(clang::tooling::newFrontendActionFactory(&finder).get());
  const auto ParseElapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - ParseStart)
          .count();

  alchemy::logger::info(
      "alchemy::{}parser{}::direct [{}]: AST parse completed ({}ms)\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      std::filesystem::path(header).filename().string(),
      ParseElapsedMs);

  if (clangResult != 0)
  {
    return alchemy::core::
        Result<std::vector<alchemy::parser::artifacts::StructDef>>::failure(
            alchemy::core::Error::format(
                "alchemy::parser::ClangParser",
                "direct header '{}': reported fatal errors during parsing "
                "(exit code: {})\n"
                "  hint: files with errors above may need to be added to "
                "the exclude list in .alchemy/alchemy.toml\n"
                "  note: parsing cannot continue with fatal errors, "
                "type information would be incorrect",
                header,
                clangResult));
  }

  auto structs = structParser->getParsedStructs();
  for (auto& s : structs)
  {
    s.targetName = "default";
  }

  return alchemy::core::
      Result<std::vector<alchemy::parser::artifacts::StructDef>>::success(
          std::move(structs));
}

alchemy::core::Result<alchemy::parser::artifacts::ParseResults>
alchemy::parser::ClangParser::parse(const ParsingRequirements& requirements)
{
  alchemy::parser::artifacts::ParseResults results;

  if (!requirements.needsStructParsing)
  {
    return alchemy::core::Result<
        alchemy::parser::artifacts::ParseResults>::success(std::move(results));
  }

  alchemy::logger::debug("alchemy::parser::ClangParser::parse: {} spec(s), "
                         "{} direct header(s)\n",
                         m_parseCmds.size(),
                         m_directHeaders.size());

  // run one ClangTool per resolved spec (isolated, thin single-command DB).
  // failures are non-fatal: a spec may fail when the compile command contains
  // cross-target SDK include paths that cause enumerator redefinitions clang
  // rejects as standard C violations (GCC accepts these as an extension).
  // skip the offending TU, warn the user, and continue with the rest.
  std::size_t specFailures = 0;
  for (const auto& cmd : m_parseCmds)
  {
    auto parseResult = run(cmd);
    if (parseResult.invalid())
    {
      ++specFailures;
      alchemy::logger::warn(
          "alchemy::{}parser{}::{}warning{}: target '{}' [{}] "
          "skipped: {}\n"
          "  note: this is often caused by cross-target includes\n"
          "  fix:  remove conflicting include paths from this "
          "target's CMakeLists.txt (if possible)\n"
          "  fix:  add the file to the exclude list in "
          ".alchemy/alchemy.toml (workaround)\n",
          alchemy::color::ansi::BoldBrightGreen,
          alchemy::color::ansi::Reset,
          alchemy::color::ansi::Yellow,
          alchemy::color::ansi::Reset,
          cmd.targetName,
          std::filesystem::path(cmd.parseFile).filename().string(),
          parseResult.error());
      continue;
    }
    auto structs = std::move(parseResult).value();
    results.structs.insert(results.structs.end(),
                           std::make_move_iterator(structs.begin()),
                           std::make_move_iterator(structs.end()));
  }

  if (specFailures > 0)
  {
    alchemy::logger::warn(
        "alchemy::{}parser{}::{}warning{}: {}{}{} spec(s) skipped "
        "due to parse errors (see above)\n",
        alchemy::color::ansi::BoldBrightGreen,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::Yellow,
        alchemy::color::ansi::Reset,
        alchemy::color::ansi::BrightGreen,
        specFailures,
        alchemy::color::ansi::Reset);
  }

  // run direct-header fallback; also non-fatal — best-effort path
  for (const auto& header : m_directHeaders)
  {
    auto parseResult = run(header);
    if (parseResult.invalid())
    {
      alchemy::logger::warn(
          "alchemy::{}parser{}::{}warning{}: direct header '{}' "
          "skipped: {}\n",
          alchemy::color::ansi::BoldBrightGreen,
          alchemy::color::ansi::Reset,
          alchemy::color::ansi::Yellow,
          alchemy::color::ansi::Reset,
          std::filesystem::path(header).filename().string(),
          parseResult.error());
      continue;
    }
    auto structs = std::move(parseResult).value();
    results.structs.insert(results.structs.end(),
                           std::make_move_iterator(structs.begin()),
                           std::make_move_iterator(structs.end()));
  }

  // hard-fail only in the degenerate case where every spec failed and
  // there are no direct headers — nothing was analysed at all
  if (!m_parseCmds.empty() && specFailures == m_parseCmds.size() &&
      m_directHeaders.empty())
  {
    return alchemy::core::Result<alchemy::parser::artifacts::ParseResults>::
        failure(alchemy::core::Error::format(
            "alchemy::parser::ClangParser",
            "all {} parse spec(s) reported fatal errors; nothing to "
            "analyze\n"
            "  hint: check the errors above for cross-target include "
            "contamination, or missing include paths",
            m_parseCmds.size()));
  }

  alchemy::logger::debug(
      "alchemy::parser::ClangParser::parse: {} raw structs from {} cmd(s)\n",
      results.structs.size(),
      m_parseCmds.size());

  deduplicateStructs(results.structs);

  return alchemy::core::Result<
      alchemy::parser::artifacts::ParseResults>::success(std::move(results));
}
