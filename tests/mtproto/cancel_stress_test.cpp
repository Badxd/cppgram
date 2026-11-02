// Stress test: many threads wait on sleep_cancellable while another thread requests stop.
// Every waiter must return or throw OperationCancelled promptly; none may hang.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stop_token>
#include <thread>
#include <vector>

#include "cppgram/mtproto/cancel.h"

using cppgram::mtproto::OperationCancelled;
using cppgram::mtproto::sleep_cancellable;

TEST(CancelStress, ConcurrentWaitersAllUnblockOnStop) {
    constexpr int kRounds = 50;
    constexpr int kWaiters = 16;
    for (int round = 0; round < kRounds; ++round) {
        std::stop_source source;
        std::atomic<int> cancelled{0};
        std::atomic<int> finished{0};
        std::vector<std::thread> waiters;
        waiters.reserve(kWaiters);
        for (int i = 0; i < kWaiters; ++i) {
            waiters.emplace_back([&, token = source.get_token()] {
                try {
                    sleep_cancellable(std::chrono::milliseconds(60'000), token);
                } catch (const OperationCancelled&) {
                    cancelled.fetch_add(1);
                }
                finished.fetch_add(1);
            });
        }
        // Give some waiters time to block, others stop before they start: both paths must finish.
        std::this_thread::sleep_for(std::chrono::milliseconds(round % 5));
        source.request_stop();
        for (auto& t : waiters) t.join();
        EXPECT_EQ(finished.load(), kWaiters);
        EXPECT_EQ(cancelled.load(), kWaiters);
    }
}

TEST(CancelStress, ShortWaitsCompleteWithoutStop) {
    std::stop_source source;
    std::vector<std::thread> waiters;
    std::atomic<int> ok{0};
    for (int i = 0; i < 8; ++i) {
        waiters.emplace_back([&, token = source.get_token()] {
            sleep_cancellable(std::chrono::milliseconds(5), token);
            ok.fetch_add(1);
        });
    }
    for (auto& t : waiters) t.join();
    EXPECT_EQ(ok.load(), 8);
}
