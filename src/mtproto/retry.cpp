#include "cppgram/mtproto/retry.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace cppgram::mtproto {

void validate_retry_policy(const RetryPolicy& policy) {
    if (policy.max_attempts < 1) throw std::invalid_argument("retry: max_attempts must be at least 1");
    if (policy.base <= std::chrono::milliseconds(0)) throw std::invalid_argument("retry: base delay must be positive");
    if (policy.cap < policy.base) throw std::invalid_argument("retry: cap must not be below base");
    if (policy.max_flood_wait < std::chrono::seconds(0)) {
        throw std::invalid_argument("retry: max_flood_wait must not be negative");
    }
}

std::chrono::milliseconds backoff_delay(const RetryPolicy& policy, int retry, double jitter01) {
    validate_retry_policy(policy);
    if (retry < 1) retry = 1;
    const double j = std::clamp(jitter01, 0.0, 1.0);

    // base * 2^(retry-1), computed in doubles so large retry counts cannot overflow; then capped.
    const double base = static_cast<double>(policy.base.count());
    const double cap = static_cast<double>(policy.cap.count());
    const double exponent = std::min(retry - 1, 62);
    const double d = std::min(cap, base * (double)(1ULL << static_cast<unsigned>(exponent)));

    const double half = d / 2.0;
    const double delay = half + j * half;
    return std::chrono::milliseconds(static_cast<std::int64_t>(delay));
}

bool may_retry(const RetryPolicy& policy, int attempts_made) noexcept {
    return attempts_made < policy.max_attempts;
}

namespace {

// Parses the number in FLOOD_WAIT_<n> / SLOWMODE_WAIT_<n>. Returns -1 if the message has no such form.
long long parse_wait_seconds(const std::string& message) {
    for (const char* prefix : {"FLOOD_WAIT_", "SLOWMODE_WAIT_"}) {
        const std::string p = prefix;
        if (message.rfind(p, 0) != 0) continue;
        const std::string digits = message.substr(p.size());
        if (digits.empty() || digits.size() > 9) return -1;
        if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return -1;
        return std::stoll(digits);
    }
    return -1;
}

}  // namespace

ErrorDecision classify_rpc_error(int code, const std::string& message) {
    ErrorDecision out;
    if (code == 420) {
        const long long secs = parse_wait_seconds(message);
        if (secs >= 0) {
            out.kind = ErrorClass::FloodWait;
            out.wait = std::chrono::seconds(secs);
        }
        return out;  // a 420 without a parseable wait is Permanent: never guess a wait time
    }
    if (code >= 500 && code < 600) {
        out.kind = ErrorClass::Transient;
        return out;
    }
    out.kind = ErrorClass::Permanent;
    return out;
}

bool flood_wait_allowed(const RetryPolicy& policy, const ErrorDecision& decision) noexcept {
    return decision.kind == ErrorClass::FloodWait && decision.wait <= policy.max_flood_wait;
}

}  // namespace cppgram::mtproto
