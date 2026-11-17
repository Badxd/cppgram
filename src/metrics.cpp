#include "cppgram/metrics.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "cppgram/log.h"

namespace cppgram::metrics {

namespace detail {

struct Series {
    std::shared_ptr<const std::atomic<bool>> enabled;
    std::vector<std::string> label_values;
    std::atomic<double> value{0.0};                   // counter, gauge
    std::vector<double> bounds;                       // histogram upper bounds
    std::vector<std::atomic<std::uint64_t>> buckets;  // histogram: bounds.size() + 1 (last is +Inf)
    std::atomic<double> sum{0.0};                     // histogram
    std::atomic<std::uint64_t> count{0};              // histogram
};

struct Family {
    std::string name;
    std::string help;
    Kind kind = Kind::Counter;
    std::vector<std::string> label_names;
    std::vector<double> bounds;
    std::size_t max_series = 0;
    std::shared_ptr<const std::atomic<bool>> enabled;
    std::shared_ptr<std::atomic<std::uint64_t>> dropped;

    mutable std::mutex mutex;
    std::map<std::vector<std::string>, std::shared_ptr<Series>> series;
    std::shared_ptr<Series> overflow;

    std::shared_ptr<Series> make_series(const std::vector<std::string>& values) const {
        auto s = std::make_shared<Series>();
        s->enabled = enabled;
        s->label_values.reserve(values.size());
        for (const auto& v : values) s->label_values.push_back(log::redact(v));
        if (kind == Kind::Histogram) {
            s->bounds = bounds;
            s->buckets = std::vector<std::atomic<std::uint64_t>>(bounds.size() + 1);
            // Explicit zeroing: value-initialisation of atomics in a vector is not guaranteed on every standard library.
            for (auto& bucket : s->buckets) bucket.store(0, std::memory_order_relaxed);
        }
        return s;
    }

    // Caller holds no lock. Returns the series for these values, or the overflow series when the cap is reached.
    std::shared_ptr<Series> get(const std::vector<std::string>& values) {
        std::lock_guard<std::mutex> lock(mutex);
        if (auto it = series.find(values); it != series.end()) return it->second;
        if (series.size() >= max_series) {
            dropped->fetch_add(1, std::memory_order_relaxed);
            if (!overflow) {
                overflow = make_series(std::vector<std::string>(label_names.size(), "__overflow__"));
            }
            return overflow;
        }
        auto s = make_series(values);
        series.emplace(values, s);
        return s;
    }
};

struct RegistryState {
    std::shared_ptr<std::atomic<bool>> enabled;
    std::shared_ptr<std::atomic<std::uint64_t>> dropped;
    std::size_t max_series = 128;
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Family>> families;
};

} // namespace detail

namespace {

bool is_alpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool is_alnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool valid_metric_name(std::string_view name) {
    if (name.empty()) return false;
    if (!(is_alpha(name[0]) || name[0] == '_' || name[0] == ':')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) { return is_alnum(c) || c == '_' || c == ':'; });
}

bool valid_label_name(std::string_view name) {
    if (name.empty()) return false;
    if (!(is_alpha(name[0]) || name[0] == '_')) return false;
    if (name.starts_with("__")) return false; // reserved for the exporter
    return std::all_of(name.begin() + 1, name.end(), [](char c) { return is_alnum(c) || c == '_'; });
}

void atomic_add(std::atomic<double>& target, double amount) noexcept {
    double old = target.load(std::memory_order_relaxed);
    while (!target.compare_exchange_weak(old, old + amount, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void check_definition(const std::string& name, const std::vector<std::string>& label_names) {
    if (!valid_metric_name(name)) throw std::invalid_argument("invalid metric name: " + name);
    for (const auto& label : label_names) {
        if (!valid_label_name(label)) throw std::invalid_argument("invalid label name in " + name + ": " + label);
    }
    for (std::size_t i = 0; i < label_names.size(); ++i) {
        for (std::size_t j = i + 1; j < label_names.size(); ++j) {
            if (label_names[i] == label_names[j]) {
                throw std::invalid_argument("duplicate label name in " + name + ": " + label_names[i]);
            }
        }
    }
}

std::shared_ptr<detail::Family> get_or_create(detail::RegistryState& state, std::string name, std::string help,
                                              Kind kind, std::vector<std::string> label_names,
                                              std::vector<double> bounds) {
    check_definition(name, label_names);
    if (kind == Kind::Histogram) {
        if (bounds.empty()) throw std::invalid_argument("histogram " + name + " needs at least one bound");
        for (std::size_t i = 0; i < bounds.size(); ++i) {
            if (!std::isfinite(bounds[i])) throw std::invalid_argument("histogram " + name + " bound is not finite");
            if (i > 0 && !(bounds[i - 1] < bounds[i])) {
                throw std::invalid_argument("histogram " + name + " bounds must be strictly ascending");
            }
        }
    }

    std::lock_guard<std::mutex> lock(state.mutex);
    for (const auto& existing : state.families) {
        if (existing->name != name) continue;
        if (existing->kind != kind || existing->label_names != label_names || existing->bounds != bounds) {
            throw std::invalid_argument("metric " + name + " is already registered with a different definition");
        }
        return existing;
    }

    auto family = std::make_shared<detail::Family>();
    family->name = std::move(name);
    family->help = std::move(help);
    family->kind = kind;
    family->label_names = std::move(label_names);
    family->bounds = std::move(bounds);
    family->max_series = state.max_series;
    family->enabled = state.enabled;
    family->dropped = state.dropped;
    state.families.push_back(family);
    return family;
}

std::shared_ptr<detail::Series> resolve(const std::shared_ptr<detail::Family>& family,
                                        const std::vector<std::string>& values) {
    if (!family) return nullptr;
    if (values.size() != family->label_names.size()) {
        throw std::invalid_argument("metric " + family->name + " expects " +
                                    std::to_string(family->label_names.size()) + " label value(s)");
    }
    return family->get(values);
}

} // namespace

// ---- series handles

void Counter::inc(double amount) const noexcept {
    if (!series_ || !(amount >= 0.0) || !series_->enabled->load(std::memory_order_relaxed)) return;
    atomic_add(series_->value, amount);
}

double Counter::value() const noexcept {
    return series_ ? series_->value.load(std::memory_order_relaxed) : 0.0;
}

void Gauge::set(double value) const noexcept {
    if (!series_ || std::isnan(value) || !series_->enabled->load(std::memory_order_relaxed)) return;
    series_->value.store(value, std::memory_order_relaxed);
}

void Gauge::add(double delta) const noexcept {
    if (!series_ || std::isnan(delta) || !series_->enabled->load(std::memory_order_relaxed)) return;
    atomic_add(series_->value, delta);
}

double Gauge::value() const noexcept {
    return series_ ? series_->value.load(std::memory_order_relaxed) : 0.0;
}

void Histogram::observe(double value) const noexcept {
    if (!series_ || std::isnan(value) || !series_->enabled->load(std::memory_order_relaxed)) return;
    const auto& bounds = series_->bounds;
    // First bound >= value. When value is above every bound, the index lands on the +Inf bucket.
    const auto index = static_cast<std::size_t>(std::lower_bound(bounds.begin(), bounds.end(), value) - bounds.begin());
    series_->buckets[index].fetch_add(1, std::memory_order_relaxed);
    atomic_add(series_->sum, value);
    series_->count.fetch_add(1, std::memory_order_relaxed);
}

// ---- families

Counter CounterFamily::labels(const std::vector<std::string>& values) const {
    return Counter(resolve(family_, values));
}

Gauge GaugeFamily::labels(const std::vector<std::string>& values) const {
    return Gauge(resolve(family_, values));
}

Histogram HistogramFamily::labels(const std::vector<std::string>& values) const {
    return Histogram(resolve(family_, values));
}

// ---- registry

Registry::Registry(RegistryOptions options) : state_(std::make_shared<detail::RegistryState>()) {
    state_->enabled = std::make_shared<std::atomic<bool>>(options.enabled);
    state_->dropped = std::make_shared<std::atomic<std::uint64_t>>(0);
    state_->max_series = std::max<std::size_t>(options.max_series_per_family, 1);
}

Registry::~Registry() = default;

CounterFamily Registry::counter(std::string name, std::string help, std::vector<std::string> label_names) {
    return CounterFamily(get_or_create(*state_, std::move(name), std::move(help), Kind::Counter,
                                       std::move(label_names), {}));
}

GaugeFamily Registry::gauge(std::string name, std::string help, std::vector<std::string> label_names) {
    return GaugeFamily(get_or_create(*state_, std::move(name), std::move(help), Kind::Gauge, std::move(label_names),
                                     {}));
}

HistogramFamily Registry::histogram(std::string name, std::string help, std::vector<double> bounds,
                                    std::vector<std::string> label_names) {
    return HistogramFamily(get_or_create(*state_, std::move(name), std::move(help), Kind::Histogram,
                                         std::move(label_names), std::move(bounds)));
}

void Registry::set_enabled(bool enabled) noexcept {
    state_->enabled->store(enabled, std::memory_order_relaxed);
}

bool Registry::enabled() const noexcept {
    return state_->enabled->load(std::memory_order_relaxed);
}

std::uint64_t Registry::dropped_series() const noexcept {
    return state_->dropped->load(std::memory_order_relaxed);
}

std::vector<FamilySnapshot> Registry::snapshot() const {
    std::vector<std::shared_ptr<detail::Family>> families;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        families = state_->families;
    }

    std::vector<FamilySnapshot> out;
    out.reserve(families.size());
    for (const auto& family : families) {
        FamilySnapshot fs;
        fs.name = family->name;
        fs.help = family->help;
        fs.kind = family->kind;
        fs.label_names = family->label_names;
        fs.bounds = family->bounds;

        std::lock_guard<std::mutex> lock(family->mutex);
        const auto read = [&](const detail::Series& s) {
            SeriesSnapshot ss;
            ss.label_values = s.label_values;
            if (family->kind == Kind::Histogram) {
                ss.bucket_counts.reserve(s.buckets.size());
                for (const auto& b : s.buckets) ss.bucket_counts.push_back(b.load(std::memory_order_relaxed));
                ss.sum = s.sum.load(std::memory_order_relaxed);
                ss.count = s.count.load(std::memory_order_relaxed);
            } else {
                ss.value = s.value.load(std::memory_order_relaxed);
            }
            fs.series.push_back(std::move(ss));
        };
        for (const auto& [values, s] : family->series) read(*s);
        if (family->overflow) read(*family->overflow);
        out.push_back(std::move(fs));
    }
    return out;
}

} // namespace cppgram::metrics
