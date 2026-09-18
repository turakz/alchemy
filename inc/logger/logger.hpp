#ifndef ALCHEMY_APP_LOGGER_HPP
#define ALCHEMY_APP_LOGGER_HPP

// std
#include <cstdint>
#include <cstdio>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// 3rd party
#include <fmt/core.h>

namespace alchemy::logger {

enum class Level : std::uint8_t { Debug, Info, Warn, Error };

namespace detail {

class Logger {
public:
  static Logger&
  instance();

  void
  init(const std::filesystem::path& logFile);
  void
  shutdown();
  void
  write(Level level, const std::string& message);
  void
  setLevel(Level level);
  bool
  isEnabled(Level level) const;

private:
  FILE* m_logFile = nullptr;
  std::mutex m_mutex;
  std::atomic<Level> m_minLevel{Level::Info};

  static std::string
  stripAnsi(const std::string& text);
  static const char*
  levelTag(Level level);
};

}  // namespace detail

// non-template functions — defined in logger.cpp
void
init(const std::filesystem::path& logFile);
void
shutdown();
void
setLevel(Level level);

// format a list of strings as "[item1, item2, item3]"
std::string
formatList(const std::vector<std::string>& items);

// template functions — must be in header for instantiation
template <typename... Args>
void
debug(fmt::format_string<Args...> fmt, Args&&... args)
{
  if (detail::Logger::instance().isEnabled(Level::Debug))
  {
    detail::Logger::instance().write(
        Level::Debug, fmt::format(fmt, std::forward<Args>(args)...));
  }
}

template <typename... Args>
void
info(fmt::format_string<Args...> fmt, Args&&... args)
{
  if (detail::Logger::instance().isEnabled(Level::Info))
  {
    detail::Logger::instance().write(
        Level::Info, fmt::format(fmt, std::forward<Args>(args)...));
  }
}

template <typename... Args>
void
warn(fmt::format_string<Args...> fmt, Args&&... args)
{
  if (detail::Logger::instance().isEnabled(Level::Warn))
  {
    detail::Logger::instance().write(
        Level::Warn, fmt::format(fmt, std::forward<Args>(args)...));
  }
}

template <typename... Args>
void
error(fmt::format_string<Args...> fmt, Args&&... args)
{
  if (detail::Logger::instance().isEnabled(Level::Error))
  {
    detail::Logger::instance().write(
        Level::Error, fmt::format(fmt, std::forward<Args>(args)...));
  }
}

}  // namespace alchemy::logger
#endif  // ALCHEMY_APP_LOGGER_HPP
