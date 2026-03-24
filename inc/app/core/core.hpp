#ifndef ALCHEMY_APP_CORE_HPP
#define ALCHEMY_APP_CORE_HPP

// std
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

// 3rd party
#include "fmt/core.h"

// local
#include "app/color.hpp"

namespace alchemy::core {

namespace filesystem {
struct TempFile {
  std::filesystem::path path;
  explicit TempFile(std::string_view prefix, std::string_view ext)
  {
    path = std::filesystem::temp_directory_path() /
           fmt::format("{}{}", prefix, ext);
  }
  TempFile() = default;
  TempFile(const TempFile&) = default;
  TempFile&
  operator=(const TempFile&) = default;
  TempFile(TempFile&&) = default;
  TempFile&
  operator=(TempFile&&) = default;
  ~TempFile()
  {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec)
    {
      fmt::print(stderr,
                 "alchemy::core::filesystem::TempFile::~TempFile: "
                 "failed to remove {}: {}\n",
                 path.string(),
                 ec.message());
    }
  }
};
};  // namespace filesystem

struct Error {

  template <typename... Args>
  static std::string
  format(std::string_view context,
         fmt::format_string<Args...> fmt,
         Args&&... args)
  {
    return fmt::format("{}::{}error{}::{}",
                       context,
                       alchemy::color::ansi::Magenta,
                       alchemy::color::ansi::Reset,
                       fmt::format(fmt, std::forward<Args>(args)...)) +
           "\n";
  }

  std::string message;
  explicit Error(std::string msg) : message(std::move(msg))
  {
  }
};

template <typename T>
class Result {
public:
  static Result
  success(const T& value)
  {
    Result result;
    result.m_data = value;
    return result;
  }
  static Result
  success(T&& value)
  {
    Result result;
    result.m_data = std::move(value);
    return result;
  }

  static Result
  failure(const std::string& msg)
  {
    Result result;
    result.m_data = alchemy::core::Error(msg);
    return result;
  }
  static Result
  failure(std::string&& msg)
  {
    Result result;
    result.m_data = alchemy::core::Error(std::move(msg));
    return result;
  }

  [[nodiscard]] bool
  valid() const
  {
    return std::holds_alternative<T>(m_data);
  }
  [[nodiscard]] bool
  invalid() const
  {
    return !valid();
  }

  // lvalue accessors - return references (for lvalue Results)
  [[nodiscard]] const T&
  value() const&
  {
    return std::get<T>(m_data);
  }
  [[nodiscard]] T&
  value() &
  {
    return std::get<T>(m_data);
  }
  [[nodiscard]] const std::string&
  error() const&
  {
    return std::get<alchemy::core::Error>(m_data).message;
  }

  // rvalue accessors - move out of temporaries (for rvalue Results)
  [[nodiscard]] T&&
  value() &&
  {
    return std::move(std::get<T>(m_data));
  }
  [[nodiscard]] std::string
  error() &&
  {
    return std::move(std::get<alchemy::core::Error>(m_data).message);
  }

  [[nodiscard]] std::optional<std::reference_wrapper<const T>>
  tryValue() const
  {
    if (valid())
    {
      return std::cref(std::get<T>(m_data));
    }
    return std::nullopt;
  }

private:
  Result() : m_data(alchemy::core::Error("m_data::uninitialized"))
  {
  }
  std::variant<T, alchemy::core::Error> m_data;
};

}  // namespace alchemy::core
#endif  // ALCHEMY_APP_CORE_HPP
