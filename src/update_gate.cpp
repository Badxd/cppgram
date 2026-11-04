#include "cppgram/update_gate.h"

namespace cppgram {

PtsDecision classify_pts(std::int32_t local_pts, std::int32_t pts, std::int32_t pts_count) noexcept {
    // Use 64-bit arithmetic so a hostile pts_count near INT32_MAX cannot overflow.
    const std::int64_t expected = static_cast<std::int64_t>(local_pts) + static_cast<std::int64_t>(pts_count);
    const std::int64_t got = static_cast<std::int64_t>(pts);
    if (got == expected) return PtsDecision::Apply;
    if (expected > got) return PtsDecision::Duplicate;
    return PtsDecision::Gap;
}

}  // namespace cppgram
