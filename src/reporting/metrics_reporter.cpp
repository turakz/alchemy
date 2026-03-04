// src/metrics_reporter.cpp
#include "reporting/metrics_reporter.hpp"

// std
#include <type_traits>
#include <variant>
#include <vector>

// 3rd party

// local
#include "metrics/metrics.hpp"
#include "metrics/salign_metrics.hpp"
#include "reporting/salign_reporter.hpp"

// dispatcher -> separates variants and calls type-specific reporters
void
alchemy::metrics::reporter::reportMetrics(
    const std::vector<alchemy::metrics::Metrics>& metrics)
{
  if (metrics.empty())
  {
    return;
  }

  // partition metrics by variant type
  std::vector<alchemy::metrics::detail::SAlignMetrics> salignMetrics;

  for (const auto& metric : metrics)
  {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T,
                                       alchemy::metrics::detail::SAlignMetrics>)
          {
            salignMetrics.push_back(value);
          }
        },
        metric);
  }

  if (!salignMetrics.empty())
  {
    alchemy::metrics::reporter::SAlignReporter::report(salignMetrics);
  }
}
