#ifndef ALCHEMY_APP_APP_HPP
#define ALCHEMY_APP_APP_HPP

// std
#include <utility>
#include <vector>

// 3rd party

// local
#include "app/core/core.hpp"
#include "cli/cli.hpp"
#include "config/config.hpp"
#include "operation/operation.hpp"

namespace alchemy {

class App {
public:
  [[nodiscard]] static alchemy::core::Result<alchemy::App>
  create(alchemy::cli::ParsedOptions options);

  // move-only semantics (avoid copying expensive objects)
  ~App() = default;
  App(const App&) = delete;
  App&
  operator=(const App&) = delete;
  App(App&&) noexcept = default;
  App&
  operator=(App&&) noexcept = default;

  int
  exec();

  alchemy::config::AppConfig&
  context() noexcept
  {
    return m_context;
  }

  const alchemy::config::AppConfig&
  context() const noexcept
  {
    return m_context;
  }

  std::vector<alchemy::operation::RecipeOperation>
  createRecipeOperations(const alchemy::config::AppConfig& context);

private:
  // only factory method can create App instances
  explicit App(alchemy::config::AppConfig&& context) noexcept
    : m_context(std::move(context))
  {
  }

  alchemy::config::AppConfig m_context;
};

};  // namespace alchemy
#endif  // ALCHEMY_APP_APP_HPP
