// cppgram - metrics: counters, gauges and histograms with fixed label names and a per-family series cap.
//
// Thread safety: every handle may be used from any thread. Updates are lock-free (atomics). Creating a new label
// combination takes a per-family mutex. snapshot() may run while other threads update; each value it reads is
// atomic, but a histogram's buckets, sum and count are read one after another, so a snapshot taken during an
// observation can be off by that one observation.
//
// Ownership: handles hold shared ownership of their storage, so a handle stays valid after its Registry is
// destroyed. Updates through it are then simply not exported.
//
// No-op handles: a default-constructed Counter, Gauge, Histogram or family does nothing. Instrumented code can
// hold such handles when metrics are off and skip null checks.
//
// Sensitive data: label values pass through log::redact() before they are stored, so a bot token or a
// "key=value" secret that ends up in a label is masked. Use fixed, low-cardinality values anyway.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cppgram::metrics {

enum class Kind : std::uint8_t { Counter, Gauge, Histogram };

struct RegistryOptions {
    // Distinct label-value combinations kept per family. Further combinations fold into one overflow series
    // whose label values are all "__overflow__". Each folded update is counted by Registry::dropped_series().
    std::size_t max_series_per_family = 128;
    // When false, every update is ignored. Registration and snapshot still work.
    bool enabled = true;
};

struct SeriesSnapshot {
    std::vector<std::string> label_values;
    double value = 0.0;                          // counter and gauge
    std::vector<std::uint64_t> bucket_counts;    // histogram: one per bound, then +Inf. Not cumulative.
    double sum = 0.0;                            // histogram
    std::uint64_t count = 0;                     // histogram
};

struct FamilySnapshot {
    std::string name;
    std::string help;
    Kind kind = Kind::Counter;
    std::vector<std::string> label_names;
    std::vector<double> bounds;                  // histogram upper bounds, ascending
    std::vector<SeriesSnapshot> series;          // sorted by label values; the overflow series comes last
};

namespace detail {
struct Series;
struct Family;
struct RegistryState;
} // namespace detail

class CounterFamily;
class GaugeFamily;
class HistogramFamily;

class Counter {
public:
    Counter() = default;
    // Adds a non-negative amount. Negative or NaN amounts are ignored.
    void inc(double amount = 1.0) const noexcept;
    double value() const noexcept;
    explicit operator bool() const noexcept { return static_cast<bool>(series_); }

private:
    friend class CounterFamily;
    explicit Counter(std::shared_ptr<detail::Series> series) : series_(std::move(series)) {}
    std::shared_ptr<detail::Series> series_;
};

class Gauge {
public:
    Gauge() = default;
    void set(double value) const noexcept;
    void add(double delta) const noexcept;
    double value() const noexcept;
    explicit operator bool() const noexcept { return static_cast<bool>(series_); }

private:
    friend class GaugeFamily;
    explicit Gauge(std::shared_ptr<detail::Series> series) : series_(std::move(series)) {}
    std::shared_ptr<detail::Series> series_;
};

class Histogram {
public:
    Histogram() = default;
    // Ignores NaN. A value goes into the first bucket whose bound is >= value, or into +Inf.
    void observe(double value) const noexcept;
    explicit operator bool() const noexcept { return static_cast<bool>(series_); }

private:
    friend class HistogramFamily;
    explicit Histogram(std::shared_ptr<detail::Series> series) : series_(std::move(series)) {}
    std::shared_ptr<detail::Series> series_;
};

class CounterFamily {
public:
    CounterFamily() = default;
    // values must match the family's label names in number and order. Throws std::invalid_argument otherwise.
    Counter labels(const std::vector<std::string>& values) const;
    explicit operator bool() const noexcept { return static_cast<bool>(family_); }

private:
    friend class Registry;
    explicit CounterFamily(std::shared_ptr<detail::Family> family) : family_(std::move(family)) {}
    std::shared_ptr<detail::Family> family_;
};

class GaugeFamily {
public:
    GaugeFamily() = default;
    Gauge labels(const std::vector<std::string>& values) const;
    explicit operator bool() const noexcept { return static_cast<bool>(family_); }

private:
    friend class Registry;
    explicit GaugeFamily(std::shared_ptr<detail::Family> family) : family_(std::move(family)) {}
    std::shared_ptr<detail::Family> family_;
};

class HistogramFamily {
public:
    HistogramFamily() = default;
    Histogram labels(const std::vector<std::string>& values) const;
    explicit operator bool() const noexcept { return static_cast<bool>(family_); }

private:
    friend class Registry;
    explicit HistogramFamily(std::shared_ptr<detail::Family> family) : family_(std::move(family)) {}
    std::shared_ptr<detail::Family> family_;
};

class Registry {
public:
    explicit Registry(RegistryOptions options = {});
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // Registering the same name, kind, label names and bounds again returns the existing family.
    // Any other redefinition of a name, an invalid metric or label name, and invalid histogram bounds
    // throw std::invalid_argument.
    CounterFamily counter(std::string name, std::string help, std::vector<std::string> label_names = {});
    GaugeFamily gauge(std::string name, std::string help, std::vector<std::string> label_names = {});
    HistogramFamily histogram(std::string name, std::string help, std::vector<double> bounds,
                              std::vector<std::string> label_names = {});

    void set_enabled(bool enabled) noexcept;
    bool enabled() const noexcept;

    // Copies the current state. Safe to call from any thread at any time.
    std::vector<FamilySnapshot> snapshot() const;

    // Number of updates that went to the overflow series because a family hit max_series_per_family.
    std::uint64_t dropped_series() const noexcept;

private:
    std::shared_ptr<detail::RegistryState> state_;
};

} // namespace cppgram::metrics
