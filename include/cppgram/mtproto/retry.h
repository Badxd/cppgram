// cppgram - retry policy: bounded exponential backoff with jitter, and RPC error classification.
// Pure functions: no sockets, no sleeping. Callers decide when to sleep and whether to resend.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace cppgram::mtproto {

struct RetryPolicy {
    int max_attempts = 3;                              // total tries, including the first one
    std::chrono::milliseconds base{500};               // delay before the first retry
    std::chrono::milliseconds cap{10000};              // upper bound for any single delay
    std::chrono::seconds max_flood_wait{60};           // longer FLOOD_WAIT is returned to the caller
};

// Throws std::invalid_argument for an unusable policy (max_attempts < 1, base <= 0, cap < base).
void validate_retry_policy(const RetryPolicy& policy);

// Delay before retry number `retry` (1 = first retry). Exponential from `base`, capped at `cap`,
// with "equal jitter": the delay is in [d/2, d], where d is the capped exponential value.
// `jitter01` is a uniform value in [0, 1]; pass a random value in production and fixed values in tests.
std::chrono::milliseconds backoff_delay(const RetryPolicy& policy, int retry, double jitter01);

// True while another attempt is allowed after `attempts_made` tries.
bool may_retry(const RetryPolicy& policy, int attempts_made) noexcept;

enum class ErrorClass {
    Transient,   // network trouble or server 5xx: retry with backoff
    FloodWait,   // server asked us to wait `wait_seconds`: retry after that, within max_flood_wait
    Permanent,   // auth or request error: do not retry
};

struct ErrorDecision {
    ErrorClass kind = ErrorClass::Permanent;
    std::chrono::seconds wait{0};  // only meaningful for FloodWait
};

// Classifies a Telegram RPC error by its code and message. FLOOD_WAIT_N and SLOWMODE_WAIT_N (code 420)
// yield FloodWait with N seconds; 5xx yield Transient; everything else is Permanent.
ErrorDecision classify_rpc_error(int code, const std::string& message);

// Returns true when a FloodWait decision is within the policy's limit, so the caller may wait and retry.
bool flood_wait_allowed(const RetryPolicy& policy, const ErrorDecision& decision) noexcept;

// Counts consecutive failed reconnect attempts for one outage. Reset after a successful packet.
struct ReconnectBudget {
    int max_failures = 5;
    int failures = 0;

    // Records one failed attempt. Returns false once the budget is spent (the caller must give up).
    bool record_failure() noexcept {
        ++failures;
        return failures <= max_failures;
    }
    void reset() noexcept { failures = 0; }
};

}  // namespace cppgram::mtproto
