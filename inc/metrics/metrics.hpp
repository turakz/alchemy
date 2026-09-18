#ifndef ALCHEMY_METRICS_METRICS_HPP
#define ALCHEMY_METRICS_METRICS_HPP

// std
#include <variant>

// 3rd party

// local
#include "metrics/salign_metrics.hpp"

namespace alchemy::metrics {

// variant type - unified metrics representation
using Metrics = std::variant<SAlignMetrics>;

}  // namespace alchemy::metrics
#endif  // ALCHEMY_METRICS_METRICS_HPP
