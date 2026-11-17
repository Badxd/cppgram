#include <gtest/gtest.h>

#include <chrono>
#include <stop_token>
#include <thread>

#include "cppgram/mtproto/cancel.h"

using namespace cppgram::mtproto;
using namespace std::chrono;

TEST(Cancel, SleepCompletesWhenNotStopped) {
    std::stop_source src;
    const auto t0 = steady_clock::now();
    EXPECT_NO_THROW(sleep_cancellable(milliseconds(30), src.get_token()));
    EXPECT_GE(steady_clock::now() - t0, milliseconds(25));
}

TEST(Cancel, AlreadyStoppedThrowsWithoutSleeping) {
    std::stop_source src;
    src.request_stop();
    const auto t0 = steady_clock::now();
    EXPECT_THROW(sleep_cancellable(seconds(10), src.get_token()), OperationCancelled);
    EXPECT_LT(steady_clock::now() - t0, milliseconds(500));
}

TEST(Cancel, StopFromAnotherThreadEndsALongSleepEarly) {
    std::stop_source src;
    std::thread stopper([&src] {
        std::this_thread::sleep_for(milliseconds(50));
        src.request_stop();
    });
    const auto t0 = steady_clock::now();
    EXPECT_THROW(sleep_cancellable(seconds(30), src.get_token()), OperationCancelled);
    stopper.join();
    // A 30 s backoff must not hold up shutdown; allow generous slack for slow CI machines.
    EXPECT_LT(steady_clock::now() - t0, seconds(5));
}
