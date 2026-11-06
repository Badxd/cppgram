#include "cppgram/metrics_prometheus.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cppgram::metrics {

namespace {

std::string escape_help(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string escape_label_value(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// Shortest round-trip form. Non-finite values use the spellings the exposition format defines.
std::string format_number(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "+Inf" : "-Inf";
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

std::string format_count(std::uint64_t value) {
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, result.ptr);
}

// Writes {name="value",...}. extra, when non-empty, is an additional pair (used for le).
void write_labels(std::string& out, const std::vector<std::string>& names, const std::vector<std::string>& values,
                  std::string_view extra_name = {}, std::string_view extra_value = {}) {
    if (names.empty() && extra_name.empty()) return;
    out += '{';
    bool first = true;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!first) out += ',';
        first = false;
        out += names[i];
        out += "=\"";
        out += escape_label_value(values[i]);
        out += '"';
    }
    if (!extra_name.empty()) {
        if (!first) out += ',';
        out += extra_name;
        out += "=\"";
        out += extra_value;
        out += '"';
    }
    out += '}';
}

std::string_view kind_name(Kind kind) {
    switch (kind) {
    case Kind::Counter: return "counter";
    case Kind::Gauge: return "gauge";
    case Kind::Histogram: return "histogram";
    }
    return "untyped";
}

} // namespace

std::string render_prometheus(const std::vector<FamilySnapshot>& families) {
    std::string out;
    for (const auto& family : families) {
        out += "# HELP ";
        out += family.name;
        out += ' ';
        out += escape_help(family.help);
        out += '\n';
        out += "# TYPE ";
        out += family.name;
        out += ' ';
        out += kind_name(family.kind);
        out += '\n';

        for (const auto& series : family.series) {
            if (family.kind == Kind::Histogram) {
                std::uint64_t cumulative = 0;
                for (std::size_t i = 0; i < family.bounds.size(); ++i) {
                    cumulative += series.bucket_counts[i];
                    out += family.name;
                    out += "_bucket";
                    write_labels(out, family.label_names, series.label_values, "le", format_number(family.bounds[i]));
                    out += ' ';
                    out += format_count(cumulative);
                    out += '\n';
                }
                cumulative += series.bucket_counts.back();
                out += family.name;
                out += "_bucket";
                write_labels(out, family.label_names, series.label_values, "le", "+Inf");
                out += ' ';
                out += format_count(cumulative);
                out += '\n';

                out += family.name;
                out += "_sum";
                write_labels(out, family.label_names, series.label_values);
                out += ' ';
                out += format_number(series.sum);
                out += '\n';

                out += family.name;
                out += "_count";
                write_labels(out, family.label_names, series.label_values);
                out += ' ';
                out += format_count(series.count);
                out += '\n';
            } else {
                out += family.name;
                write_labels(out, family.label_names, series.label_values);
                out += ' ';
                out += format_number(series.value);
                out += '\n';
            }
        }
    }
    return out;
}

std::string render_prometheus(const Registry& registry) {
    return render_prometheus(registry.snapshot());
}

} // namespace cppgram::metrics
