// inc/metrics_reporter.hpp
#ifndef ALCHEMY_REPORTING_METRICS_REPORTER_HPP
#define ALCHEMY_REPORTING_METRICS_REPORTER_HPP

// std
#include <vector>

// 3rd party

// local
#include "metrics/metrics.hpp"

namespace alchemy::metrics::reporter {

// dispatcher - separates variants and calls appropriate report function
void
reportMetrics(const std::vector<alchemy::metrics::Metrics>& metrics);

}  // namespace alchemy::metrics::reporter
#endif  // ALCHEMY_REPORTING_METRICS_REPORTER_HPP
