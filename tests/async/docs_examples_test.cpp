// Compiles and runs the examples shown in docs/ASYNC.md, docs/METRICS.md and docs/MEDIA.md, so the documentation
// cannot drift from the API. Offline only. Nothing here reaches Telegram.
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

#include "cppgram/async.h"
#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/metrics.h"
#include "cppgram/metrics_prometheus.h"
#include "cppgram/task.h"

using namespace std::chrono_literals;
namespace async = cppgram::async;

namespace {

// ---- docs/ASYNC.md: futures on a runner

TEST(DocsExamples, RunnerPostAndGet) {
    async::Runner runner;
    async::AsyncResult<int> result = runner.post([](std::stop_token) {
        return 6 * 7;
    });
    EXPECT_EQ(result.get(), 42);
}

TEST(DocsExamples, LinkedCallerTokenCancelsARunningOperation) {
    async::Runner runner;
    std::stop_source caller;
    std::promise<void> started;
    auto started_future = started.get_future();
    async::AsyncResult<int> result = runner.post([&started](std::stop_token token) {
        started.set_value();
        while (!token.stop_requested())
            std::this_thread::sleep_for(1ms);
        return 0; // returns normally after the stop: no exception
    });
    result.link(caller.get_token()); // stopping the caller's source cancels the operation
    started_future.wait(); // a stop before the start would cancel it without running it
    caller.request_stop();
    EXPECT_EQ(result.get(), 0);
}

// ---- docs/ASYNC.md: coroutines

async::Task<int> add(int a, int b) {
    co_return a + b;
}

async::Task<int> sum_three(int a, int b, int c) {
    const int ab = co_await add(a, b);
    const int abc = co_await add(ab, c);
    co_return abc;
}

async::Task<int> cancellable_count(int limit) {
    const std::stop_token token = co_await async::this_task_stop_token();
    int done = 0;
    while (done < limit && !token.stop_requested()) {
        ++done;
        std::this_thread::sleep_for(1ms);
    }
    co_return done;
}

TEST(DocsExamples, SpawnCoroutineAndAwaitNested) {
    async::Runner runner;
    async::AsyncResult<int> result = async::spawn(runner, sum_three(1, 2, 3));
    EXPECT_EQ(result.get(), 6);
}

TEST(DocsExamples, SpawnedCoroutineSeesCancellation) {
    async::Runner runner;
    async::AsyncResult<int> result = async::spawn(runner, cancellable_count(1'000'000));
    std::this_thread::sleep_for(5ms);
    result.cancel();
    EXPECT_LT(result.get(), 1'000'000);
}

// ---- docs/ASYNC.md: Client wrappers (compiled, not run against a server)

cppgram::ClientOptions example_options() {
    cppgram::ClientOptions options;
    options.api_id = 12345;
    options.api_hash = "0123456789abcdef0123456789abcdef"; // placeholder, never used to connect here
    options.session_path = (std::filesystem::temp_directory_path() / "cppgram_docs_example.session").string();
    return options;
}

TEST(DocsExamples, ClientAsyncWrapperRunsOnItsOwnRunner) {
    cppgram::Client client(example_options());
    auto result = client.async([](cppgram::Client&, std::stop_token) {
        return std::string("ok");
    });
    EXPECT_EQ(result.get(), "ok");
}

// Compile-time check of the signatures shown in the docs. Not called.
[[maybe_unused]] void compile_only_client_examples(cppgram::Client& client, const std::string& path) {
    async::AsyncResult<void> connected = client.connect_async();
    auto sent = client.save_file_async(path, [](std::int64_t, std::int64_t) {
    });
    connected.cancel();
    (void)sent;
}

// ---- docs/METRICS.md

TEST(DocsExamples, RegistryCountersAndPrometheusText) {
    cppgram::metrics::Registry registry;
    auto requests = registry.counter("app_requests_total", "Requests handled.", { "route" });
    auto latency = registry.histogram("app_latency_seconds", "Latency.", { 0.1, 1.0 });
    requests.labels({ "/home" }).inc();
    latency.labels({}).observe(0.25);

    const std::string text = cppgram::metrics::render_prometheus(registry);
    EXPECT_NE(text.find("# TYPE app_requests_total counter"), std::string::npos);
    EXPECT_NE(text.find("app_requests_total{route=\"/home\"} 1"), std::string::npos);
    EXPECT_NE(text.find("app_latency_seconds_bucket{le=\"+Inf\"} 1"), std::string::npos);
}

TEST(DocsExamples, ClientMetricsRegistryOption) {
    auto registry = std::make_shared<cppgram::metrics::Registry>();
    cppgram::ClientOptions options = example_options();
    options.metrics = registry;
    cppgram::Client client(options);
    EXPECT_NO_THROW(cppgram::metrics::render_prometheus(*registry));
}

} // namespace
