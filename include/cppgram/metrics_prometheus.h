// cppgram - Prometheus text exposition (format 0.0.4). Rendering only: no HTTP server is started here.
// Serve the returned string yourself, with content type kPrometheusContentType, from any HTTP stack you already use.
#pragma once

#include <string>
#include <vector>

#include "cppgram/metrics.h"

namespace cppgram::metrics {

inline constexpr const char* kPrometheusContentType = "text/plain; version=0.0.4; charset=utf-8";

// Renders a snapshot of the registry. An empty registry renders an empty string.
// Each family gets "# HELP" and "# TYPE" lines. Histograms render cumulative _bucket series with le labels
// (ending in le="+Inf"), then _sum and _count. Label values escape backslash, double quote and newline. HELP text
// escapes backslash and newline.
std::string render_prometheus(const Registry& registry);
std::string render_prometheus(const std::vector<FamilySnapshot>& families);

} // namespace cppgram::metrics
