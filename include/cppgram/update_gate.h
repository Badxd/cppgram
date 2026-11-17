// cppgram - update sequencing gate: classifies an update's pts against the local pts.
// Pure function, so the duplicate and gap rules can be tested without a connection.
#pragma once

#include <cstdint>

namespace cppgram {

enum class PtsDecision {
    Apply,      // pts == local + pts_count: next in sequence, apply and advance
    Duplicate,  // already applied (pts before the expected value): drop
    Gap,        // updates were missed: fetch the difference, do not apply this one yet
};

// `local_pts` is the last applied pts. Updates carry `pts` (the value after applying) and `pts_count`
// (how many steps they represent). Matches Telegram's rule: next expected pts is local + pts_count.
PtsDecision classify_pts(std::int32_t local_pts, std::int32_t pts, std::int32_t pts_count) noexcept;

}  // namespace cppgram
