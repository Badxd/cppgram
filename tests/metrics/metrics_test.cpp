// Offline tests for the metrics core and the Prometheus exposition. No network, no credentials.
#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "cppgram/bot/Api.h"
#include "cppgram/bot/CurlHttpClient.h"
#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/TgException.h"
#include "cppgram/client.h"
#include "cppgram/log.h"
#include "cppgram/metrics.h"
#include "cppgram/metrics_prometheus.h"

namespace {

using namespace cppgram::metrics;

// Returns by value: the snapshot it reads from is usually a temporary.
SeriesSnapshot only_series(const std::vector<FamilySnapshot>& snap, const std::string& name) {
    for (const auto& family : snap) {
        if (family.name == name) {
            EXPECT_EQ(family.series.size(), 1u);
            return family.series.empty() ? SeriesSnapshot{} : family.series.front();
        }
    }
    ADD_FAILURE() << "family not found: " << name;
    return {};
}

TEST(MetricsCounter, IncrementsAndIgnoresNegativeAndNaN) {
    Registry registry;
    const Counter c = registry.counter("test_total", "A test counter.").labels({});
    c.inc();
    c.inc(2.5);
    c.inc(-4.0);
    c.inc(std::nan(""));
    EXPECT_DOUBLE_EQ(c.value(), 3.5);
    EXPECT_DOUBLE_EQ(only_series(registry.snapshot(), "test_total").value, 3.5);
}

TEST(MetricsGauge, SetAndAddTrackTheLatestValue) {
    Registry registry;
    const Gauge g = registry.gauge("test_gauge", "A test gauge.").labels({});
    g.set(10);
    g.add(-3);
    g.add(0.5);
    EXPECT_DOUBLE_EQ(g.value(), 7.5);
    g.set(std::nan(""));
    EXPECT_DOUBLE_EQ(g.value(), 7.5) << "NaN must not overwrite a gauge";
}

TEST(MetricsHistogram, ObservationsFallIntoTheRightBuckets) {
    Registry registry;
    const Histogram h = registry.histogram("test_seconds", "A test histogram.", {0.1, 1.0, 10.0}).labels({});
    h.observe(0.05); // <= 0.1
    h.observe(0.1);  // boundary value goes to the 0.1 bucket
    h.observe(0.5);  // <= 1.0
    h.observe(20.0); // +Inf
    h.observe(std::nan(""));  // ignored
    const auto s = only_series(registry.snapshot(), "test_seconds");
    ASSERT_EQ(s.bucket_counts.size(), 4u);
    EXPECT_EQ(s.bucket_counts[0], 2u);
    EXPECT_EQ(s.bucket_counts[1], 1u);
    EXPECT_EQ(s.bucket_counts[2], 0u);
    EXPECT_EQ(s.bucket_counts[3], 1u);
    EXPECT_EQ(s.count, 4u);
    EXPECT_DOUBLE_EQ(s.sum, 0.05 + 0.1 + 0.5 + 20.0);
}

TEST(MetricsRegistry, RegisteringTheSameDefinitionReturnsTheSameSeries) {
    Registry registry;
    const Counter a = registry.counter("same_total", "help", {"kind"}).labels({"x"});
    const Counter b = registry.counter("same_total", "help", {"kind"}).labels({"x"});
    a.inc();
    b.inc();
    EXPECT_DOUBLE_EQ(a.value(), 2.0);
}

TEST(MetricsRegistry, ConflictingRedefinitionThrows) {
    Registry registry;
    registry.counter("conflict_total", "help");
    EXPECT_THROW(registry.gauge("conflict_total", "help"), std::invalid_argument);
    EXPECT_THROW(registry.counter("conflict_total", "help", {"extra"}), std::invalid_argument);
}

TEST(MetricsRegistry, InvalidNamesAndBoundsAreRejected) {
    Registry registry;
    EXPECT_THROW(registry.counter("", "h"), std::invalid_argument);
    EXPECT_THROW(registry.counter("1starts_with_digit", "h"), std::invalid_argument);
    EXPECT_THROW(registry.counter("has-dash", "h"), std::invalid_argument);
    EXPECT_THROW(registry.counter("ok_total", "h", {"__reserved"}), std::invalid_argument);
    EXPECT_THROW(registry.counter("ok_total", "h", {"a", "a"}), std::invalid_argument);
    EXPECT_THROW(registry.histogram("h_seconds", "h", {}), std::invalid_argument);
    EXPECT_THROW(registry.histogram("h_seconds", "h", {1.0, 1.0}), std::invalid_argument);
    EXPECT_THROW(registry.histogram("h_seconds", "h", {2.0, 1.0}), std::invalid_argument);
    EXPECT_THROW(registry.histogram("h_seconds", "h", {std::numeric_limits<double>::infinity()}), std::invalid_argument);
}

TEST(MetricsRegistry, WrongLabelCountThrows) {
    Registry registry;
    const CounterFamily family = registry.counter("labelled_total", "h", {"a", "b"});
    EXPECT_THROW(family.labels({"only-one"}), std::invalid_argument);
    EXPECT_THROW(family.labels({}), std::invalid_argument);
    EXPECT_NO_THROW(family.labels({"x", "y"}));
}

TEST(MetricsRegistry, CardinalityCapFoldsIntoOverflowAndCountsDrops) {
    RegistryOptions options;
    options.max_series_per_family = 2;
    Registry registry(options);
    const CounterFamily family = registry.counter("capped_total", "h", {"method"});
    family.labels({"a"}).inc();
    family.labels({"b"}).inc();
    family.labels({"c"}).inc(5); // over the cap: folds into the overflow series
    family.labels({"d"}).inc(7);
    family.labels({"a"}).inc(); // an existing series still works after the cap

    EXPECT_EQ(registry.dropped_series(), 2u);
    const auto snap = registry.snapshot();
    ASSERT_EQ(snap[0].series.size(), 3u);
    EXPECT_EQ(snap[0].series[0].label_values, std::vector<std::string>{"a"});
    EXPECT_DOUBLE_EQ(snap[0].series[0].value, 2.0);
    EXPECT_EQ(snap[0].series[2].label_values, std::vector<std::string>{"__overflow__"});
    EXPECT_DOUBLE_EQ(snap[0].series[2].value, 12.0);
}

TEST(MetricsRegistry, DisabledRegistryIgnoresUpdatesButStillSnapshots) {
    RegistryOptions options;
    options.enabled = false;
    Registry registry(options);
    const Counter c = registry.counter("off_total", "h").labels({});
    const Gauge g = registry.gauge("off_gauge", "h").labels({});
    const Histogram h = registry.histogram("off_seconds", "h", {1.0}).labels({});
    c.inc();
    g.set(9);
    h.observe(0.5);
    EXPECT_DOUBLE_EQ(c.value(), 0.0);
    EXPECT_DOUBLE_EQ(g.value(), 0.0);
    EXPECT_EQ(only_series(registry.snapshot(), "off_seconds").count, 0u);

    registry.set_enabled(true);
    c.inc();
    EXPECT_DOUBLE_EQ(c.value(), 1.0);
}

TEST(MetricsRegistry, DefaultHandlesAreNoOps) {
    Counter c;
    Gauge g;
    Histogram h;
    EXPECT_FALSE(static_cast<bool>(c));
    c.inc();
    g.set(1);
    h.observe(1);
    EXPECT_DOUBLE_EQ(c.value(), 0.0);
    EXPECT_DOUBLE_EQ(g.value(), 0.0);
}

TEST(MetricsRegistry, EmptyRegistrySnapshotIsEmpty) {
    Registry registry;
    EXPECT_TRUE(registry.snapshot().empty());
    EXPECT_EQ(render_prometheus(registry), "");
}

TEST(MetricsRegistry, HandleOutlivesRegistry) {
    Counter c;
    {
        Registry registry;
        c = registry.counter("outlive_total", "h").labels({});
    }
    c.inc(); // must not crash; the update is simply not exported anywhere
    EXPECT_DOUBLE_EQ(c.value(), 1.0);
}

TEST(MetricsConcurrency, ConcurrentUpdatesAreNotLost) {
    Registry registry;
    const CounterFamily family = registry.counter("race_total", "h", {"worker"});
    const Histogram h = registry.histogram("race_seconds", "h", {0.5}).labels({});
    constexpr int kThreads = 8;
    constexpr int kOps = 20000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            const Counter mine = family.labels({"shared"});
            for (int i = 0; i < kOps; ++i) {
                mine.inc();
                h.observe(i % 2 == 0 ? 0.1 : 1.0);
            }
            (void)t;
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_DOUBLE_EQ(family.labels({"shared"}).value(), static_cast<double>(kThreads * kOps));
    const auto s = only_series(registry.snapshot(), "race_seconds");
    EXPECT_EQ(s.count, static_cast<std::uint64_t>(kThreads * kOps));
    EXPECT_EQ(s.bucket_counts[0] + s.bucket_counts[1], static_cast<std::uint64_t>(kThreads * kOps));
}

TEST(MetricsConcurrency, SnapshotRunsWhileOtherThreadsUpdateAndCreateSeries) {
    Registry registry;
    const CounterFamily family = registry.counter("live_total", "h", {"n"});
    std::thread writer([&] {
        for (int i = 0; i < 5000; ++i) family.labels({std::to_string(i % 50)}).inc();
    });
    for (int i = 0; i < 200; ++i) {
        const auto snap = registry.snapshot();
        ASSERT_EQ(snap.size(), 1u);
    }
    writer.join();
    EXPECT_EQ(registry.snapshot()[0].series.size(), 50u);
}

TEST(PrometheusFormat, CounterGaugeAndHelpTypeLines) {
    Registry registry;
    registry.counter("cppgram_rpc_requests_total", "RPC requests sent.").labels({}).inc(3);
    registry.gauge("cppgram_connected", "1 when connected.").labels({}).set(1);
    const std::string text = render_prometheus(registry);
    EXPECT_EQ(text,
              "# HELP cppgram_rpc_requests_total RPC requests sent.\n"
              "# TYPE cppgram_rpc_requests_total counter\n"
              "cppgram_rpc_requests_total 3\n"
              "# HELP cppgram_connected 1 when connected.\n"
              "# TYPE cppgram_connected gauge\n"
              "cppgram_connected 1\n");
}

TEST(PrometheusFormat, HistogramRendersCumulativeBucketsSumAndCount) {
    Registry registry;
    const HistogramFamily family = registry.histogram("req_seconds", "Latency.", {0.5, 1.0}, {"method"});
    family.labels({"getMe"}).observe(0.25);
    family.labels({"getMe"}).observe(0.75);
    family.labels({"getMe"}).observe(3.0);
    const std::string text = render_prometheus(registry);
    EXPECT_NE(text.find("# TYPE req_seconds histogram\n"), std::string::npos);
    EXPECT_NE(text.find("req_seconds_bucket{method=\"getMe\",le=\"0.5\"} 1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("req_seconds_bucket{method=\"getMe\",le=\"1\"} 2\n"), std::string::npos) << text;
    EXPECT_NE(text.find("req_seconds_bucket{method=\"getMe\",le=\"+Inf\"} 3\n"), std::string::npos) << text;
    EXPECT_NE(text.find("req_seconds_sum{method=\"getMe\"} 4\n"), std::string::npos) << text;
    EXPECT_NE(text.find("req_seconds_count{method=\"getMe\"} 3\n"), std::string::npos) << text;
}

TEST(PrometheusFormat, LabelValuesAndHelpAreEscaped) {
    Registry registry;
    registry.counter("esc_total", "Line one\nback\\slash", {"v"}).labels({"quote\" back\\ new\nline"}).inc();
    const std::string text = render_prometheus(registry);
    EXPECT_NE(text.find("# HELP esc_total Line one\\nback\\\\slash\n"), std::string::npos) << text;
    EXPECT_NE(text.find("esc_total{v=\"quote\\\" back\\\\ new\\nline\"} 1\n"), std::string::npos) << text;
}

TEST(PrometheusFormat, NonFiniteGaugeValuesUseExpositionSpellings) {
    Registry registry;
    const Gauge g = registry.gauge("inf_gauge", "h").labels({});
    g.set(std::numeric_limits<double>::infinity());
    EXPECT_NE(render_prometheus(registry).find("inf_gauge +Inf\n"), std::string::npos);
    g.set(-std::numeric_limits<double>::infinity());
    EXPECT_NE(render_prometheus(registry).find("inf_gauge -Inf\n"), std::string::npos);
}

TEST(PrometheusFormat, TokenLikeLabelValuesAreRedacted) {
    Registry registry;
    const std::string token = "123456789:AAExampleSecretPart_abc-123456789";
    registry.counter("bot_http_requests_total", "h", {"token"}).labels({token}).inc();
    const std::string text = render_prometheus(registry);
    EXPECT_EQ(text.find(token), std::string::npos) << text;
    EXPECT_EQ(text.find("AAExampleSecretPart"), std::string::npos) << text;
}

using cppgram::Client;
using cppgram::ClientOptions;
namespace bot = cppgram::bot;
namespace mtproto = cppgram::mtproto;

double series_value(const std::vector<FamilySnapshot>& snap, const std::string& name,
                    const std::vector<std::string>& values) {
    for (const auto& family : snap) {
        if (family.name != name) continue;
        for (const auto& series : family.series) {
            if (series.label_values == values) return series.value;
        }
    }
    return -1.0; // absent
}

TEST(ClientMetrics, FailedConnectRecordsAttemptTransportErrorAndDisconnectedGauge) {
    auto registry = std::make_shared<Registry>();
    ClientOptions options;
    options.api_id = 12345;
    options.api_hash = "0123456789abcdef0123456789abcdef";
    options.dc_ip = "127.0.0.1";
    options.dc_port = 1; // nothing listens here
    options.connect_timeout_ms = 1000;
    options.retry.max_attempts = 1;
    options.session_path = (std::filesystem::temp_directory_path() / "cppgram_metrics_test.session").string();
    options.metrics = registry;

    Client client(options);
    EXPECT_THROW(client.connect(), mtproto::ConnectionError);

    const auto snap = registry->snapshot();
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_connection_attempts_total", {"2"}), 1.0);
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_connections_total", {"2"}), -1.0) << "no connection succeeded";
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_connected", {}), 0.0);
    EXPECT_GE(series_value(snap, "cppgram_transport_errors_total", {"network"}), 0.0)
        << render_prometheus(*registry);
    const std::string text = render_prometheus(*registry);
    EXPECT_EQ(text.find("0123456789abcdef0123456789abcdef"), std::string::npos) << text;
}

TEST(ClientMetrics, DisabledClientMetricsRecordNothing) {
    ClientOptions options;
    options.api_id = 12345;
    options.api_hash = "0123456789abcdef0123456789abcdef";
    options.dc_ip = "127.0.0.1";
    options.dc_port = 1;
    options.connect_timeout_ms = 1000;
    options.retry.max_attempts = 1;
    options.session_path = (std::filesystem::temp_directory_path() / "cppgram_metrics_off.session").string();
    Client client(options); // no registry at all
    EXPECT_THROW(client.connect(), mtproto::ConnectionError);
}

class MetricsFixedHttp final : public bot::HttpClient {
public:
    explicit MetricsFixedHttp(std::string body) : body_(std::move(body)) {}
    std::string makeRequest(const std::string&, std::span<const bot::HttpFormField>) const override { return body_; }

private:
    std::string body_;
};

class MetricsApiProbe : public bot::Api {
public:
    using bot::Api::Api;
    nlohmann::json call(std::string_view method) const { return sendRequest(method, {}); }
};

TEST(BotApiMetrics, FailedRequestRecordsMethodErrorCodeAndDurationWithoutToken) {
    const std::string token = "123456789:AAExampleSecretPartForMetricsTest_1234567890";
    auto registry = std::make_shared<Registry>();
    MetricsFixedHttp http(R"({"ok":false,"error_code":401,"description":"Unauthorized"})");
    MetricsApiProbe api(token, http, "https://api.telegram.org");
    api.set_metrics(registry);

    EXPECT_THROW(api.call("getMe"), cppgram::bot::TgException);

    const auto snap = registry->snapshot();
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_bot_requests_total", {"getMe"}), 1.0);
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_bot_api_errors_total", {"401"}), 1.0);
    const std::string text = render_prometheus(*registry);
    EXPECT_NE(text.find("cppgram_bot_request_duration_seconds_count{method=\"getMe\"} 1\n"), std::string::npos) << text;
    EXPECT_EQ(text.find(token), std::string::npos) << text;
    EXPECT_EQ(text.find("AAExampleSecretPart"), std::string::npos) << text;
}

TEST(BotApiMetrics, TransportFailureRecordsStatusZeroAndTransportError) {
    auto registry = std::make_shared<Registry>();
    bot::CurlHttpClient http;
    http.set_metrics(registry);
    EXPECT_THROW(http.makeRequest("http://127.0.0.1:1/getMe", {}), std::exception);

    const auto snap = registry->snapshot();
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_bot_http_responses_total", {"0"}), 1.0);
    EXPECT_DOUBLE_EQ(series_value(snap, "cppgram_bot_http_timeouts_total", {}), 0.0);
}

} // namespace
