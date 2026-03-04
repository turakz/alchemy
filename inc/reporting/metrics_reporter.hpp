// inc/metrics_reporter.hpp
#ifndef ALCHEMY_REPORTING_METRICS_REPORTER_HPP
#define ALCHEMY_REPORTING_METRICS_REPORTER_HPP

// std
#include <vector>

// 3rd party

// local
#include "metrics/metrics.hpp"
#include "metrics/salign_metrics.hpp"

namespace alchemy::metrics::reporter {

// report overloads: operations produce metrics (variants) - batch reporting
void
report(const std::vector<alchemy::metrics::detail::SAlignMetrics>& metrics);

// dispatcher - separates variants and calls appropriate report function
void
reportMetrics(const std::vector<alchemy::metrics::Metrics>& metrics);

}  // namespace alchemy::metrics::reporter
#endif  // ALCHEMY_REPORTING_METRICS_REPORTER_HPP
