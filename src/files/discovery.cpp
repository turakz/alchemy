// src/files/discovery.cpp
#include "files/discovery.hpp"

// std
#include <cstddef>
#include <cstdio>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <regex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// local
#include "app/color.hpp"
#include "app/core/core.hpp"
#include "logger/logger.hpp"

// helper: find common ancestor directory of multiple paths
std::filesystem::path
alchemy::discovery::detail::findCommonAncestor(
    const std::vector<std::filesystem::path>& paths)
{
  if (paths.empty())
  {
    return std::filesystem::current_path();
  }
  if (paths.size() == 1)
  {
    return paths.front();
  }
  auto commonAncestor = std::filesystem::path(paths.front());
  for (const auto& path : paths)
  {
    while (!path.string().starts_with(commonAncestor.string()) &&
           !commonAncestor.empty())
    {
      commonAncestor = commonAncestor.parent_path();
    }
    if (commonAncestor.empty())
    {
      return std::filesystem::current_path();
    }
  }
  return commonAncestor;
}

// phase 1: build discovery configuration from include/exclude patterns
alchemy::discovery::DiscoveryConfig
alchemy::discovery::buildDiscoveryConfig(
    const std::vector<std::string>& includePatterns,
    const std::vector<std::string>& excludePatterns)
{
  DiscoveryConfig config;
  config.searchRoot =
      std::filesystem::current_path();  // default to current directory
  config.needsRecursiveSearch = false;

  // combine all patterns for extension and recursive analysis
  std::vector<std::string> allPatterns;
  allPatterns.reserve(includePatterns.size() + excludePatterns.size());
  allPatterns.insert(std::end(allPatterns),
                     std::begin(includePatterns),
                     std::end(includePatterns));
  allPatterns.insert(std::end(allPatterns),
                     std::begin(excludePatterns),
                     std::end(excludePatterns));

  // check if any pattern (include OR exclude) needs recursive search
  config.needsRecursiveSearch = std::any_of(
      std::begin(allPatterns), std::end(allPatterns), [](const auto& pattern) {
        return pattern.find("**") != std::string::npos;
      });

  // extract file extensions from all patterns (both include and exclude)
  // only consider the filename component (after last '/') to avoid
  // extracting directory names like ".venv" as extensions
  for (const auto& pattern : allPatterns)
  {
    auto lastSlash = pattern.find_last_of('/');
    auto filenameStart = (lastSlash != std::string::npos) ? lastSlash + 1 : 0;
    auto lastDot = pattern.find_last_of('.');
    if (lastDot != std::string::npos && lastDot >= filenameStart)
    {
      std::string extension = pattern.substr(lastDot);
      // clean up extension (remove trailing wildcards if any)
      auto wildcard = extension.find('*');
      if (wildcard != std::string::npos)
      {
        extension = extension.substr(0, wildcard);
      }
      // reject if contains '/' (directory component, not a file extension)
      if (!extension.empty() && extension.find('/') == std::string::npos)
      {
        config.targetExtensions.insert(extension);
      }
    }
  }

  // find search root from INCLUDE patterns only (excludes don't determine where
  // to search) collect all pattern roots to find common ancestor
  std::vector<std::filesystem::path> patternRoots;

  for (const auto& pattern : includePatterns)
  {
    if (pattern.front() != '*' && pattern.find('/') != std::string::npos)
    {
      const std::filesystem::path PatternPath(pattern);
      std::filesystem::path potentialRoot = PatternPath.parent_path();

      // find first directory component before any wildcards
      std::string pathStr = potentialRoot.string();
      auto firstWildcard = pathStr.find('*');
      if (firstWildcard != std::string::npos)
      {
        // construct potential root dir for the first glob level
        pathStr = pathStr.substr(0, firstWildcard);
        if (pathStr.back() == '/')
        {
          pathStr.pop_back();
        }
        potentialRoot = pathStr;
      }

      if (std::filesystem::exists(potentialRoot) &&
          std::filesystem::is_directory(potentialRoot))
      {
        patternRoots.push_back(potentialRoot);
      }
    }
  }

  // if multiple roots were found, enable recursive search
  if (patternRoots.size() > 1)
  {
    config.needsRecursiveSearch = true;
  }

  // find common ancestor of all pattern roots
  if (!patternRoots.empty())
  {
    config.searchRoot = detail::findCommonAncestor(patternRoots);
  }

  {
    const std::vector<std::string> Extensions(
        std::begin(config.targetExtensions), std::end(config.targetExtensions));
    alchemy::logger::debug("alchemy::discovery::buildDiscoveryConfig: "
                           "searchRoot={}, recursive={}, extensions={}\n",
                           config.searchRoot.string(),
                           config.needsRecursiveSearch,
                           alchemy::logger::formatList(Extensions));
  }

  return config;
}

// phase 2: collect all candidate files with target extensions
std::vector<std::filesystem::path>
alchemy::discovery::findCandidateFiles(const DiscoveryConfig& config)
{
  std::vector<std::filesystem::path> candidates;
  try
  {
    auto addCandidate = [&](auto entry) {
      if (entry.is_regular_file())
      {
        const std::string Extension = entry.path().extension().string();
        if (config.targetExtensions.contains(Extension))
        {
          candidates.emplace_back(entry.path());
        }
      }
    };
    if (config.needsRecursiveSearch)
    {
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(config.searchRoot))
      {
        addCandidate(entry);
      }
    }
    else
    {
      for (const auto& entry :
           std::filesystem::directory_iterator(config.searchRoot))
      {
        addCandidate(entry);
      }
    }
  }
  catch (const std::filesystem::filesystem_error& e)
  {
    alchemy::logger::error(
        "{}",
        alchemy::core::Error::format("alchemy::discovery::findCandidateFiles",
                                     "filesystem exception caught: {}",
                                     e.what()));
  }

  alchemy::logger::debug(
      "alchemy::discovery::findCandidateFiles: found {} candidates in {}\n",
      candidates.size(),
      config.searchRoot.string());

  return candidates;
}

// phase 3 helper: convert glob pattern to regex for std::regex matching
std::string
alchemy::discovery::globToRegex(std::string_view pattern)
{
  std::string result;
  result.reserve(pattern.size() * 2);

  for (size_t i = 0; i < pattern.size(); ++i)
  {
    const char C = pattern[i];
    switch (C)
    {
      case '*':
        if (i + 1 < pattern.size() && pattern[i + 1] == '*')
        {
          // handle ** (recursive wildcard — zero or more directories)
          // if followed by '/', consume it and make the whole group optional
          // so that "dir/**/*.h" matches both "dir/foo.h" and "dir/sub/foo.h"
          if (i + 2 < pattern.size() && pattern[i + 2] == '/')
          {
            result += "(.*/)?";
            i += 2;  // skip the second * and the /
          }
          else
          {
            result += ".*";
            ++i;  // skip the second *
          }
        }
        else
        {
          // handle single * (non-recursive wildcard)
          result += "[^/]*";
        }
        break;
      case '?':
        result += "[^/]";
        break;
      case '.':
      case '^':
      case '$':
      case '+':
      case '|':
      case '{':
      case '}':
      case '[':
      case ']':
      case '(':
      case ')':
      case '\\':
        result += '\\';
        result += C;
        break;
      default:
        result += C;
        break;
    }
  }
  return "^" + result + "$";
}

// pre-compile patterns for efficient matching
std::vector<alchemy::discovery::CompiledPattern>
alchemy::discovery::compilePatterns(const std::vector<std::string>& patterns)
{
  std::vector<CompiledPattern> compiled;
  compiled.reserve(patterns.size());

  for (const auto& pattern : patterns)
  {
    CompiledPattern cp;
    cp.original = pattern;

    try
    {
      const std::string RegexPattern = globToRegex(pattern);
      cp.compiledRegex = std::regex(RegexPattern);
    }
    catch (const std::regex_error& e)
    {
      alchemy::logger::error("{}",
                             alchemy::core::Error::format(
                                 "alchemy::discovery",
                                 "regex compilation error for pattern '{}': {}",
                                 pattern,
                                 e.what()));
      // leave compiledRegex in default state, will fail matching
    }

    compiled.push_back(std::move(cp));
  }

  return compiled;
}

// match file against pre-compiled pattern
bool
alchemy::discovery::matchesPattern(const std::filesystem::path& file,
                                   const CompiledPattern& pattern)
{
  const std::string PathStr = file.string();
  return std::regex_match(PathStr, pattern.compiledRegex);
}

// main discovery function using collect-then-filter approach
alchemy::core::Result<alchemy::discovery::DiscoveryResult>
alchemy::discovery::discoverFiles(
    const std::vector<std::string>& sourcePatterns,
    const std::vector<std::string>& excludePatterns,
    const std::size_t Jobs)
{
  alchemy::logger::debug(
      "alchemy::discovery::discoverFiles: {} jobs, include={}, exclude={}\n",
      Jobs,
      alchemy::logger::formatList(sourcePatterns),
      alchemy::logger::formatList(excludePatterns));

  const alchemy::discovery::DiscoveryConfig Config =
      alchemy::discovery::buildDiscoveryConfig(sourcePatterns, excludePatterns);

  std::vector<std::filesystem::path> candidates =
      alchemy::discovery::findCandidateFiles(Config);

  // phase 3: pre-compile patterns once for efficient matching
  auto compiledIncludePatterns =
      alchemy::discovery::compilePatterns(sourcePatterns);
  auto compiledExcludePatterns =
      alchemy::discovery::compilePatterns(excludePatterns);

  // filter candidates in single pass using pre-compiled patterns
  std::vector<std::filesystem::path> sourceFiles;
  std::vector<std::string> sourceFilesAsStrs;
  std::vector<std::filesystem::path> excludedFiles;

  // discovery
  std::vector<std::thread> workers;
  std::vector<std::vector<std::filesystem::path>> workerMatches(Jobs);
  std::vector<std::vector<std::filesystem::path>> workerExcludes(Jobs);

  // round up in case we have fewer candidates than we have threads
  std::size_t chunkSz = (candidates.size() + Jobs - 1) / Jobs;

  for (std::size_t tIdx = 0; tIdx < Jobs; ++tIdx)
  {
    workers.emplace_back([&, tIdx]() {
      const std::size_t Start = tIdx * chunkSz;
      const std::size_t End = std::min(Start + chunkSz, candidates.size());

      auto& localSources = workerMatches[tIdx];
      auto& localExcluded = workerExcludes[tIdx];

      for (std::size_t matchesIdx = Start; matchesIdx < End; ++matchesIdx)
      {
        const auto& path = candidates[matchesIdx];
        const bool Included = std::any_of(
            std::begin(compiledIncludePatterns),
            std::end(compiledIncludePatterns),
            [&](const auto& pattern) { return matchesPattern(path, pattern); });
        const bool Excluded = std::any_of(
            std::begin(compiledExcludePatterns),
            std::end(compiledExcludePatterns),
            [&](const auto& pattern) { return matchesPattern(path, pattern); });

        if (Included && !Excluded)
        {
          localSources.push_back(path);
        }
        else if (Excluded)
        {
          localExcluded.push_back(path);
        }
      }
    });
  }
  // collect results: join all worker threads
  std::ranges::for_each(workers, [](auto& worker) { worker.join(); });
  // flatten/merge: move all worker results into final containers
  for (auto&& matches : workerMatches)
  {
    std::ranges::move(matches, std::back_inserter(sourceFiles));
  }
  for (auto&& excludes : workerExcludes)
  {
    std::ranges::move(excludes, std::back_inserter(excludedFiles));
  }

  std::ranges::sort(sourceFiles);
  // for debug output
  sourceFilesAsStrs.reserve(sourceFiles.size());
  for (const auto& file : sourceFiles)
  {
    sourceFilesAsStrs.push_back(file.string());
  }

  // log results
  alchemy::logger::debug(
      "alchemy::discovery::discoverFiles: source files: {}\n",
      alchemy::logger::formatList(sourceFilesAsStrs));
  alchemy::logger::info(
      "alchemy::{}discovery{}::found {}{}{} source files...\n",
      alchemy::color::ansi::BoldBrightGreen,
      alchemy::color::ansi::Reset,
      alchemy::color::ansi::BrightGreen,
      sourceFiles.size(),
      alchemy::color::ansi::Reset);
  alchemy::logger::debug("alchemy::discovery::discoverFiles: {} matched, {} "
                         "excluded from {} candidates\n",
                         sourceFiles.size(),
                         excludedFiles.size(),
                         candidates.size());

  static_cast<void>(std::fflush(stdout));

  DiscoveryResult result{std::move(sourceFiles), std::move(excludedFiles)};

  return core::Result<alchemy::discovery::DiscoveryResult>::success(
      std::move(result));
}
