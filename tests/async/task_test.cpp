// Coroutine tasks (cppgram/task.h): lazy start, co_await chains, exception propagation, cancellation through
// this_task_stop_token(), runner shutdown and frame lifetime. Offline only: no sockets, no Telegram.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/async.h"
#include "cppgram/errors.h"
#include "cppgram/task.h"

namespace {

using namespace std::chrono_literals;
using cppgram::async::AsyncResult;
using cppgram::async::Runner;
using cppgram::async::spawn;
using cppgram::async::Task;
using cppgram::async::this_task_stop_token;

// Counts how many live instances exist. A coroutine parameter lives in the coroutine frame, so this also checks
// that frames are destroyed.
struct Tracker {
    std::atomic<int>* live;
    explicit Tracker(std::atomic<int>& counter)
        : live(&counter) {
        live->fetch_add(1);
    }
    Tracker(Tracker&& other) noexcept
        : live(std::exchange(other.live, nullptr)) {
    }
    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;
    ~Tracker() {
        if (live)
            live->fetch_sub(1);
    }
};

struct Gate {
    std::promise<void> promise;
    std::shared_future<void> future = promise.get_future().share();
    void open() {
        promise.set_value();
    }
};

Task<int> constant(int value) {
    co_return value;
}

// Coroutine lambdas that capture state are unsafe when the closure dies before the coroutine runs, because the
// frame keeps a pointer to the closure. Every task that may outlive its creating scope therefore takes its state
// as parameters instead.
Task<void> record_order(int i, std::vector<int>* order, std::mutex* mutex) {
    std::lock_guard<std::mutex> lock(*mutex);
    order->push_back(i);
    co_return;
}

Task<int> count_and_return(int i, std::atomic<int>* total) {
    const int v = co_await constant(i);
    total->fetch_add(1);
    co_return v;
}

Task<void> do_nothing() {
    co_return;
}

Task<int> add_two(int a, int b) {
    const int x = co_await constant(a);
    const int y = co_await constant(b);
    co_return x + y;
}

TEST(CoroutineTask, SpawnedTaskReturnsItsValue) {
    Runner runner;
    auto result = spawn(runner, constant(42));
    EXPECT_EQ(result.get(), 42);
}

TEST(CoroutineTask, VoidTaskCompletes) {
    Runner runner;
    auto result = spawn(runner, do_nothing());
    result.get();
    SUCCEED();
}

TEST(CoroutineTask, IsLazyUntilSpawnedOrAwaited) {
    std::atomic<int> ran { 0 };
    auto body = [&ran]() -> Task<int> {
        ran.fetch_add(1);
        co_return 1;
    };
    {
        Task<int> task = body();
        EXPECT_TRUE(task.valid());
        EXPECT_FALSE(task.done());
        EXPECT_EQ(ran.load(), 0) << "creating a task must not run its body";
    }
    EXPECT_EQ(ran.load(), 0) << "destroying an unstarted task must not run its body";
}

TEST(CoroutineTask, UnstartedTaskDestructionReleasesItsParameters) {
    std::atomic<int> live { 0 };
    {
        auto make = [](Tracker t) -> Task<int> {
            (void)t;
            co_return 0;
        };
        Task<int> task = make(Tracker(live));
        EXPECT_EQ(live.load(), 1) << "the parameter lives in the coroutine frame";
    }
    EXPECT_EQ(live.load(), 0) << "destroying the unstarted frame must release the parameter";
}

TEST(CoroutineTask, SpawnedTaskReleasesItsFrameAfterCompletion) {
    std::atomic<int> live { 0 };
    Runner runner;
    {
        auto make = [](Tracker t) -> Task<int> {
            (void)t;
            co_return 7;
        };
        auto result = spawn(runner, make(Tracker(live)));
        EXPECT_EQ(result.get(), 7);
    }
    EXPECT_EQ(live.load(), 0);
}

TEST(CoroutineTask, ExceptionPropagatesWithItsType) {
    Runner runner;
    auto failing = []() -> Task<int> {
        throw std::invalid_argument("bad input");
        co_return 0;
    };
    auto result = spawn(runner, failing());
    EXPECT_THROW(result.get(), std::invalid_argument);
}

TEST(CoroutineTask, NestedAwaitsReturnTheCombinedValue) {
    Runner runner;
    auto outer = []() -> Task<int> {
        const int a = co_await add_two(1, 2);
        const int b = co_await add_two(a, 10);
        co_return b;
    };
    auto result = spawn(runner, outer());
    EXPECT_EQ(result.get(), 13);
}

TEST(CoroutineTask, ExceptionFromAwaitedTaskIsCatchableInTheAwaiter) {
    Runner runner;
    auto inner = []() -> Task<int> {
        throw std::out_of_range("inner failed");
        co_return 0;
    };
    auto outer = [inner]() -> Task<std::string> {
        try {
            (void)co_await inner();
        } catch (const std::out_of_range& e) {
            co_return std::string("caught: ") + e.what();
        }
        co_return std::string("not caught");
    };
    auto result = spawn(runner, outer());
    EXPECT_EQ(result.get(), "caught: inner failed");
}

TEST(CoroutineTask, ExceptionEscapesAnUncaughtAwait) {
    Runner runner;
    auto inner = []() -> Task<int> {
        throw std::runtime_error("deep");
        co_return 0;
    };
    auto outer = [inner]() -> Task<int> {
        co_return co_await inner();
    };
    auto result = spawn(runner, outer());
    EXPECT_THROW(result.get(), std::runtime_error);
}

TEST(CoroutineTask, MoveOnlyResultIsReturnedWithoutCopy) {
    Runner runner;
    auto make = []() -> Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(99);
    };
    auto result = spawn(runner, make());
    std::unique_ptr<int> value = result.get();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 99);
}

TEST(CoroutineTask, MoveOnlyResultFlowsThroughAwait) {
    Runner runner;
    auto make = []() -> Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(5);
    };
    auto outer = [make]() -> Task<int> {
        auto p = co_await make();
        co_return *p + 1;
    };
    auto result = spawn(runner, outer());
    EXPECT_EQ(result.get(), 6);
}

TEST(CoroutineTask, EmptyTaskFailsInsteadOfCrashing) {
    Runner runner;
    Task<int> empty;
    auto result = spawn(runner, std::move(empty));
    EXPECT_THROW(result.get(), std::logic_error);
}

TEST(CoroutineTask, MovedFromTaskIsEmpty) {
    Task<int> a = constant(1);
    Task<int> b = std::move(a);
    EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): the moved-from state is the behaviour under test
    EXPECT_TRUE(b.valid());
}

TEST(CoroutineCancellation, BodyReadsTheOperationStopToken) {
    Runner runner;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto body = [&started]() -> Task<int> {
        auto token = co_await this_task_stop_token();
        started.set_value();
        while (!token.stop_requested())
            std::this_thread::sleep_for(1ms);
        co_return 3;
    };
    auto result = spawn(runner, body());
    started_future.wait();
    result.cancel();
    EXPECT_EQ(result.get(), 3);
}

TEST(CoroutineCancellation, StopReachesAnAwaitedTask) {
    Runner runner;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto inner = [&started]() -> Task<bool> {
        auto token = co_await this_task_stop_token();
        started.set_value();
        while (!token.stop_requested())
            std::this_thread::sleep_for(1ms);
        co_return true;
    };
    auto outer = [inner]() -> Task<bool> {
        co_return co_await inner();
    };
    auto result = spawn(runner, outer());
    started_future.wait();
    result.cancel();
    EXPECT_TRUE(result.get()) << "the inner task must see the stop request made on the outer operation";
}

TEST(CoroutineCancellation, CancelBeforeStartNeverRunsTheBody) {
    Runner runner;
    Gate gate;
    auto blocker = runner.post([&gate](std::stop_token) {
        gate.future.wait();
    });
    std::atomic<bool> ran { false };
    auto body = [&ran]() -> Task<int> {
        ran = true;
        co_return 1;
    };
    auto result = spawn(runner, body());
    result.cancel();
    gate.open();
    blocker.get();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_FALSE(ran.load());
}

TEST(CoroutineCancellation, ShutdownCancelsQueuedTasksAndReleasesTheirFrames) {
    std::atomic<int> live { 0 };
    auto runner = std::make_unique<Runner>();
    Gate gate;
    std::promise<void> blocker_started;
    auto blocker_started_future = blocker_started.get_future();
    auto blocker = runner->post([&gate, &blocker_started](std::stop_token) {
        blocker_started.set_value();
        gate.future.wait();
    });
    blocker_started_future.wait(); // the blocker must be running, so the next task is genuinely queued
    auto make = [](Tracker t) -> Task<int> {
        (void)t;
        co_return 1;
    };
    auto pending = spawn(*runner, make(Tracker(live)));
    EXPECT_EQ(live.load(), 1);

    std::thread opener([&gate] {
        std::this_thread::sleep_for(50ms);
        gate.open();
    });
    runner.reset();
    opener.join();
    blocker.get();
    EXPECT_THROW(pending.get(), cppgram::errors::CancelledError);
    EXPECT_EQ(live.load(), 0) << "a task that never started must still release its frame";
}

TEST(CoroutineCancellation, ShutdownStopsARunningTaskAtItsNextCheck) {
    auto runner = std::make_unique<Runner>();
    std::promise<void> started;
    auto started_future = started.get_future();
    auto body = [&started]() -> Task<int> {
        auto token = co_await this_task_stop_token();
        started.set_value();
        while (!token.stop_requested())
            std::this_thread::sleep_for(1ms);
        throw std::runtime_error("woke after stop");
        co_return 0;
    };
    auto running = spawn(*runner, body());
    started_future.wait();
    const auto begin = std::chrono::steady_clock::now();
    runner.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
    EXPECT_THROW(running.get(), cppgram::errors::CancelledError);
}

TEST(CoroutineConcurrency, OneRunnerRunsTasksInFifoOrder) {
    Runner runner;
    std::vector<int> order;
    std::mutex mutex;
    std::vector<AsyncResult<void>> results;
    for (int i = 0; i < 100; ++i) {
        results.push_back(spawn(runner, record_order(i, &order, &mutex)));
    }
    for (auto& r : results)
        r.get();
    ASSERT_EQ(order.size(), 100u);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
}

TEST(CoroutineConcurrency, SeveralRunnersRunTasksFromSeveralThreads) {
    constexpr int kRunners = 4;
    constexpr int kTasksEach = 50;
    std::atomic<int> total { 0 };
    std::vector<std::thread> threads;
    std::vector<std::vector<AsyncResult<int>>> results(kRunners);
    for (int r = 0; r < kRunners; ++r) {
        threads.emplace_back([&, r] {
            Runner runner;
            for (int i = 0; i < kTasksEach; ++i) {
                results[static_cast<std::size_t>(r)].push_back(spawn(runner, count_and_return(i, &total)));
            }
            // Each runner is destroyed only after its results are consumed, so the work is not cancelled.
            long long sum = 0;
            for (auto& res : results[static_cast<std::size_t>(r)])
                sum += res.get();
            EXPECT_EQ(sum, kTasksEach * (kTasksEach - 1) / 2);
        });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(total.load(), kRunners * kTasksEach);
}

TEST(CoroutineConcurrency, CancellingOneTaskLeavesItsNeighboursRunning) {
    Runner runner;
    std::promise<void> started;
    auto started_future = started.get_future();
    auto slow = [&started]() -> Task<int> {
        auto token = co_await this_task_stop_token();
        started.set_value();
        while (!token.stop_requested())
            std::this_thread::sleep_for(1ms);
        co_return 1;
    };
    auto first = spawn(runner, slow());
    auto second = spawn(runner, constant(2));
    started_future.wait();
    first.cancel();
    EXPECT_EQ(first.get(), 1);
    EXPECT_EQ(second.get(), 2);
}

} // namespace
