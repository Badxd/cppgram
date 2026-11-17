// Deterministic offline tests for async::Runner, AsyncResult and the Client async wrappers.
// Nothing here touches the network except the connect tests, which target a closed localhost port.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/async.h"
#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/connection.h"

namespace {

using namespace std::chrono_literals;
using cppgram::async::AsyncResult;
using cppgram::async::Runner;

// Blocks until released. Used to hold the worker busy so later operations stay queued.
struct Gate {
    std::promise<void> promise;
    std::shared_future<void> future = promise.get_future().share();
    void open() { promise.set_value(); }
};

TEST(AsyncTask, CompletesWithTheReturnedValue) {
    Runner runner;
    auto result = runner.post([](std::stop_token) { return 42; });
    EXPECT_EQ(result.get(), 42);
}

TEST(AsyncTask, VoidOperationCompletes) {
    Runner runner;
    std::atomic<int> hits{0};
    auto result = runner.post([&hits](std::stop_token) { hits.fetch_add(1); });
    result.get();
    EXPECT_EQ(hits.load(), 1);
}

TEST(AsyncTask, ErrorPropagatesWithItsOriginalType) {
    Runner runner;
    auto result = runner.post([](std::stop_token) -> int { throw std::invalid_argument("bad input"); });
    EXPECT_THROW(result.get(), std::invalid_argument);
}

TEST(AsyncTask, ResultIsReadyAfterWait) {
    Runner runner;
    auto result = runner.post([](std::stop_token) { return 1; });
    result.wait();
    EXPECT_TRUE(result.ready());
    EXPECT_EQ(result.get(), 1);
    EXPECT_FALSE(result.valid()) << "get() consumes the result";
}

TEST(AsyncCancellation, CancelBeforeExecutionNeverRunsTheOperation) {
    Runner runner;
    Gate gate;
    auto blocker = runner.post([&gate](std::stop_token) { gate.future.wait(); });

    std::atomic<bool> ran{false};
    auto result = runner.post([&ran](std::stop_token) { ran = true; });
    result.cancel();
    gate.open();
    blocker.get();

    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_FALSE(ran.load());
}

TEST(AsyncCancellation, CooperativeOperationReturnsWhatItHasWhenStopped) {
    Runner runner;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto result = runner.post([&started](std::stop_token token) {
        started.set_value();
        while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
        return 7;
    });
    started_future.wait();
    result.cancel();
    EXPECT_EQ(result.get(), 7);
}

TEST(AsyncCancellation, ExceptionAfterStopIsReportedAsCancelled) {
    Runner runner;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto result = runner.post([&started](std::stop_token token) -> int {
        started.set_value();
        while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error("woke up with an error");
    });
    started_future.wait();
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
}

TEST(AsyncCancellation, LinkPropagatesACallerToken) {
    Runner runner;
    std::stop_source caller;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto result = runner.post([&started](std::stop_token token) -> int {
        started.set_value();
        while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error("stopped");
    });
    result.link(caller.get_token());
    started_future.wait();
    caller.request_stop();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
}

TEST(AsyncCancellation, LinkToAnAlreadyStoppedTokenCancelsBeforeStart) {
    Runner runner;
    Gate gate;
    auto blocker = runner.post([&gate](std::stop_token) { gate.future.wait(); });
    std::atomic<bool> ran{false};
    auto result = runner.post([&ran](std::stop_token) { ran = true; });

    std::stop_source stopped;
    stopped.request_stop();
    result.link(stopped.get_token());
    gate.open();
    blocker.get();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_FALSE(ran.load());
}

TEST(AsyncShutdown, DestroyingTheRunnerCancelsPendingAndStopsTheRunningOperation) {
    auto runner = std::make_unique<Runner>();
    std::promise<void> started;
    auto started_future = started.get_future();
    auto running = runner->post([&started](std::stop_token token) -> int {
        started.set_value();
        while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error("stopped by shutdown");
    });
    std::atomic<bool> pending_ran{false};
    auto pending = runner->post([&pending_ran](std::stop_token) { pending_ran = true; });
    started_future.wait();

    const auto begin = std::chrono::steady_clock::now();
    runner.reset(); // must return promptly, not wait for the pending operation
    const auto took = std::chrono::steady_clock::now() - begin;

    EXPECT_LT(took, 2s);
    EXPECT_THROW(running.get(), cppgram::errors::CancelledError);
    EXPECT_THROW(pending.get(), cppgram::errors::CancelledError);
    EXPECT_FALSE(pending_ran.load());
}

TEST(AsyncShutdown, ResultOutlivesTheRunner) {
    AsyncResult<int> result;
    {
        Runner runner;
        Gate gate;
        auto blocker = runner.post([&gate](std::stop_token) { gate.future.wait(); });
        result = runner.post([](std::stop_token) { return 3; });
        gate.open();
        blocker.get();
    }
    // The operation either ran before the runner was destroyed, or was cancelled. Both are valid outcomes.
    try {
        EXPECT_EQ(result.get(), 3);
    } catch (const cppgram::errors::CancelledError&) {
        SUCCEED();
    }
}

TEST(AsyncConcurrency, OneRunnerExecutesInFifoOrder) {
    Runner runner;
    std::vector<int> order;
    std::mutex order_mutex;
    std::vector<AsyncResult<void>> results;
    for (int i = 0; i < 200; ++i) {
        results.push_back(runner.post([i, &order, &order_mutex](std::stop_token) {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(i);
        }));
    }
    for (auto& r : results) r.get();
    ASSERT_EQ(order.size(), 200u);
    for (int i = 0; i < 200; ++i) EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
}

TEST(AsyncConcurrency, ManyThreadsPostAndEveryOperationCompletes) {
    Runner runner;
    std::atomic<int> total{0};
    std::vector<std::thread> posters;
    std::vector<std::vector<AsyncResult<void>>> per_thread(4);
    for (int t = 0; t < 4; ++t) {
        posters.emplace_back([&, t] {
            for (int i = 0; i < 250; ++i) {
                per_thread[static_cast<std::size_t>(t)].push_back(
                    runner.post([&total](std::stop_token) { total.fetch_add(1); }));
            }
        });
    }
    for (auto& th : posters) th.join();
    for (auto& results : per_thread) {
        for (auto& r : results) r.get();
    }
    EXPECT_EQ(total.load(), 1000);
}

TEST(AsyncNoThreadUntilUsed, IdleRunnerStartsNoWorker) {
    // Constructing and destroying an unused runner must not hang or spawn work.
    const auto begin = std::chrono::steady_clock::now();
    { Runner runner; }
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 1s);
}

cppgram::ClientOptions closed_port_options(const std::string& session_name) {
    cppgram::ClientOptions options;
    options.api_id = 12345;
    options.api_hash = "0123456789abcdef0123456789abcdef";
    options.dc_ip = "127.0.0.1";
    options.dc_port = 1; // nothing listens here
    options.connect_timeout_ms = 1000;
    options.retry.max_attempts = 1;
    options.session_path = (std::filesystem::temp_directory_path() / session_name).string();
    return options;
}

TEST(AsyncClient, GenericOperationReturnsItsValue) {
    cppgram::Client client(closed_port_options("cppgram_async_generic.session"));
    auto result = client.async([](cppgram::Client&, std::stop_token) { return 5; });
    EXPECT_EQ(result.get(), 5);
}

TEST(AsyncClient, ConnectFailureArrivesAsTheSameTypedError) {
    cppgram::Client client(closed_port_options("cppgram_async_connect.session"));
    auto result = client.connect_async();
    EXPECT_THROW(result.get(), cppgram::mtproto::ConnectionError);
}

TEST(AsyncClient, CancellingConnectDuringBackoffEndsPromptly) {
    auto options = closed_port_options("cppgram_async_backoff.session");
    options.retry.max_attempts = 3;
    options.retry.base = std::chrono::milliseconds(5000); // the first failure sleeps for seconds
    options.retry.cap = std::chrono::milliseconds(5000);
    cppgram::Client client(options);

    auto result = client.connect_async();
    std::this_thread::sleep_for(200ms); // let the first attempt fail and enter its backoff
    const auto begin = std::chrono::steady_clock::now();
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
}

TEST(AsyncClient, DestroyingClientWithPendingOperationsDoesNotHang) {
    auto client = std::make_unique<cppgram::Client>(closed_port_options("cppgram_async_destroy.session"));
    Gate gate;
    auto blocker = client->async([&gate](cppgram::Client&, std::stop_token) { gate.future.wait(); });
    auto queued = client->async([](cppgram::Client&, std::stop_token) { return 1; });
    std::thread opener([&gate] {
        std::this_thread::sleep_for(100ms);
        gate.open();
    });
    const auto begin = std::chrono::steady_clock::now();
    client.reset();
    opener.join();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 5s);
    EXPECT_THROW(queued.get(), cppgram::errors::CancelledError);
}

} // namespace
