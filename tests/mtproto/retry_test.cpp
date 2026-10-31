#include <gtest/gtest.h>

#include <stdexcept>

#include "cppgram/mtproto/retry.h"

using namespace cppgram::mtproto;
using std::chrono::milliseconds;
using std::chrono::seconds;

TEST(Retry, BackoffGrowsExponentiallyThenCaps) {
    RetryPolicy p;
    p.base = milliseconds(500);
    p.cap = milliseconds(4000);
    // jitter01 = 1 gives the top of the equal-jitter range, i.e. d itself.
    EXPECT_EQ(backoff_delay(p, 1, 1.0), milliseconds(500));
    EXPECT_EQ(backoff_delay(p, 2, 1.0), milliseconds(1000));
    EXPECT_EQ(backoff_delay(p, 3, 1.0), milliseconds(2000));
    EXPECT_EQ(backoff_delay(p, 4, 1.0), milliseconds(4000));
    EXPECT_EQ(backoff_delay(p, 5, 1.0), milliseconds(4000));  // capped
}

TEST(Retry, JitterStaysInEqualJitterRange) {
    RetryPolicy p;
    p.base = milliseconds(1000);
    p.cap = milliseconds(8000);
    // Retry 3 has d = 4000 ms, so the delay must be in [2000, 4000].
    EXPECT_EQ(backoff_delay(p, 3, 0.0), milliseconds(2000));
    EXPECT_EQ(backoff_delay(p, 3, 0.5), milliseconds(3000));
    EXPECT_EQ(backoff_delay(p, 3, 1.0), milliseconds(4000));
    // Out-of-range jitter is clamped, never produces a delay outside the range.
    EXPECT_EQ(backoff_delay(p, 3, -7.0), milliseconds(2000));
    EXPECT_EQ(backoff_delay(p, 3, 9.0), milliseconds(4000));
}

TEST(Retry, HugeRetryCountDoesNotOverflow) {
    RetryPolicy p;
    EXPECT_EQ(backoff_delay(p, 10000, 1.0), p.cap);
    EXPECT_GE(backoff_delay(p, 10000, 0.0), p.cap / 2);
}

TEST(Retry, MayRetryIsBoundedByMaxAttempts) {
    RetryPolicy p;
    p.max_attempts = 3;
    EXPECT_TRUE(may_retry(p, 1));
    EXPECT_TRUE(may_retry(p, 2));
    EXPECT_FALSE(may_retry(p, 3));
    EXPECT_FALSE(may_retry(p, 100));
}

TEST(Retry, InvalidPoliciesAreRejected) {
    RetryPolicy p;
    p.max_attempts = 0;
    EXPECT_THROW(validate_retry_policy(p), std::invalid_argument);
    p = RetryPolicy{};
    p.base = milliseconds(0);
    EXPECT_THROW(validate_retry_policy(p), std::invalid_argument);
    p = RetryPolicy{};
    p.cap = milliseconds(100);
    p.base = milliseconds(500);
    EXPECT_THROW(validate_retry_policy(p), std::invalid_argument);
    EXPECT_THROW(backoff_delay(p, 1, 0.5), std::invalid_argument);
}

TEST(Retry, FloodWaitIsParsedAndWithinLimit) {
    const ErrorDecision d = classify_rpc_error(420, "FLOOD_WAIT_37");
    EXPECT_EQ(d.kind, ErrorClass::FloodWait);
    EXPECT_EQ(d.wait, seconds(37));
    RetryPolicy p;
    p.max_flood_wait = seconds(60);
    EXPECT_TRUE(flood_wait_allowed(p, d));
    p.max_flood_wait = seconds(10);
    EXPECT_FALSE(flood_wait_allowed(p, d));  // too long: surface it, do not sleep
}

TEST(Retry, SlowmodeWaitIsFloodWaitClass) {
    const ErrorDecision d = classify_rpc_error(420, "SLOWMODE_WAIT_5");
    EXPECT_EQ(d.kind, ErrorClass::FloodWait);
    EXPECT_EQ(d.wait, seconds(5));
}

TEST(Retry, MalformedFloodWaitIsPermanent) {
    // Never guess a wait time from a malformed message.
    EXPECT_EQ(classify_rpc_error(420, "FLOOD_WAIT_").kind, ErrorClass::Permanent);
    EXPECT_EQ(classify_rpc_error(420, "FLOOD_WAIT_x9").kind, ErrorClass::Permanent);
    EXPECT_EQ(classify_rpc_error(420, "FLOOD_WAIT_1234567890").kind, ErrorClass::Permanent);
    EXPECT_EQ(classify_rpc_error(420, "SOMETHING_ELSE").kind, ErrorClass::Permanent);
}

TEST(Retry, ServerErrorsAreTransientAndAuthErrorsPermanent) {
    EXPECT_EQ(classify_rpc_error(500, "INTERNAL").kind, ErrorClass::Transient);
    EXPECT_EQ(classify_rpc_error(503, "TIMEOUT").kind, ErrorClass::Transient);
    EXPECT_EQ(classify_rpc_error(401, "AUTH_KEY_UNREGISTERED").kind, ErrorClass::Permanent);
    EXPECT_EQ(classify_rpc_error(400, "PHONE_NUMBER_INVALID").kind, ErrorClass::Permanent);
}

TEST(ReconnectBudget, SpendsAfterMaxFailuresAndResets) {
    ReconnectBudget b{3, 0};
    EXPECT_TRUE(b.record_failure());
    EXPECT_TRUE(b.record_failure());
    EXPECT_TRUE(b.record_failure());
    EXPECT_FALSE(b.record_failure());  // fourth failure: give up
    EXPECT_EQ(b.failures, 4);
    b.reset();
    EXPECT_TRUE(b.record_failure());
    EXPECT_EQ(b.failures, 1);
}

TEST(ReconnectBudget, ZeroBudgetGivesUpImmediately) {
    ReconnectBudget b{0, 0};
    EXPECT_FALSE(b.record_failure());
}
