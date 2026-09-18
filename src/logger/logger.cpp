// src/logger/logger.cpp
#include "logger/logger.hpp"

// std
#include <chrono>
#include <cstdio>
#include <ctime>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// 3rd party
#include <fmt/core.h>

alchemy::logger::detail::Logger&
alchemy::logger::detail::Logger::instance()
{
  static alchemy::logger::detail::Logger logger;
  return logger;
}

void
alchemy::logger::detail::Logger::init(const std::filesystem::path& logFile)
{
  const std::lock_guard<std::mutex> Lock(m_mutex);
  if (m_logFile != nullptr)
  {
    return;
  }

  const auto ParentDir = logFile.parent_path();
  if (!ParentDir.empty())
  {
    std::filesystem::create_directories(ParentDir);
  }

  m_logFile = std::fopen(logFile.string().c_str(), "w");

  // line-buffer stdout so console and log file stay in sync
  static_cast<void>(setvbuf(stdout, nullptr, _IOLBF, 0));
}

void
alchemy::logger::detail::Logger::shutdown()
{
  const std::lock_guard<std::mutex> Lock(m_mutex);
  if (m_logFile != nullptr)
  {
    static_cast<void>(std::fclose(m_logFile));
    m_logFile = nullptr;
  }
}

void
alchemy::logger::detail::Logger::write(Level level, const std::string& message)
{
  const std::lock_guard<std::mutex> Lock(m_mutex);

  // console output (always, even before init)
  if (level == Level::Error)
  {
    fmt::print(stderr, "{}", message);
  }
  else if (level != Level::Debug)
  {
    fmt::print("{}", message);
  }

  // log file output (only after init)
  if (m_logFile != nullptr)
  {
    const auto Now = std::chrono::system_clock::now();
    const auto TimeT = std::chrono::system_clock::to_time_t(Now);
    struct tm timeBuf {};
    localtime_r(&TimeT, &timeBuf);

    const std::string Stripped = stripAnsi(message);
    fmt::print(m_logFile,
               "[{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}] {} {}",
               timeBuf.tm_year + 1900,
               timeBuf.tm_mon + 1,
               timeBuf.tm_mday,
               timeBuf.tm_hour,
               timeBuf.tm_min,
               timeBuf.tm_sec,
               levelTag(level),
               Stripped);

    if (Stripped.empty() || Stripped.back() != '\n')
    {
      fmt::print(m_logFile, "\n");
    }

    static_cast<void>(std::fflush(m_logFile));
  }
}

std::string
alchemy::logger::detail::Logger::stripAnsi(const std::string& text)
{
  std::string result;
  result.reserve(text.size());

  for (std::size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[')
    {
      i += 2;
      while (i < text.size() && text[i] != 'm')
      {
        ++i;
      }
      continue;
    }
    result += text[i];
  }

  return result;
}

const char*
alchemy::logger::detail::Logger::levelTag(Level level)
{
  switch (level)
  {
    case Level::Debug:
      return "[DEBUG]";
    case Level::Info:
      return "[INFO]";
    case Level::Warn:
      return "[WARN]";
    case Level::Error:
      return "[ERROR]";
  }
  return "[?]";
}

void
alchemy::logger::detail::Logger::setLevel(Level level)
{
  m_minLevel.store(level, std::memory_order_relaxed);
}

bool
alchemy::logger::detail::Logger::isEnabled(Level level) const
{
  return level >= m_minLevel.load(std::memory_order_relaxed);
}

void
alchemy::logger::init(const std::filesystem::path& logFile)
{
  alchemy::logger::detail::Logger::instance().init(logFile);
}

void
alchemy::logger::shutdown()
{
  alchemy::logger::detail::Logger::instance().shutdown();
}

void
alchemy::logger::setLevel(Level level)
{
  alchemy::logger::detail::Logger::instance().setLevel(level);
}

std::string
alchemy::logger::formatList(const std::vector<std::string>& items)
{
  std::string result = "[";
  for (std::size_t i = 0; i < items.size(); ++i)
  {
    if (i > 0)
    {
      result += ", ";
    }
    result += items[i];
  }
  result += "]";
  return result;
}
