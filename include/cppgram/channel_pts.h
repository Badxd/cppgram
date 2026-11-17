// cppgram - per-channel pts tracking (pure component, not yet wired into the update loop).
// Telegram keeps a separate pts sequence for every channel and supergroup. Each channel needs its own
// baseline from updates.getChannelDifference or the channel's dialog state. Until a baseline is known, an
// update for that channel cannot be classified, so the decision is Gap: fetch the channel difference first.
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "cppgram/update_gate.h"

namespace cppgram {

class ChannelPtsTracker {
public:
    // Stores the baseline for a channel (for example from the dialog or getChannelDifference).
    void set_baseline(std::int64_t channel_id, std::int32_t pts);
    // Forgets a channel (for example after it became inaccessible). Its updates need a new baseline.
    void forget(std::int64_t channel_id);
    std::optional<std::int32_t> pts(std::int64_t channel_id) const;

    // Classifies one channel update. Unknown channels return Gap. Does not change the stored pts.
    PtsDecision classify(std::int64_t channel_id, std::int32_t pts, std::int32_t pts_count) const;
    // Records that an update was applied. Only an Apply decision should advance the stored pts.
    void mark_applied(std::int64_t channel_id, std::int32_t pts);

private:
    std::unordered_map<std::int64_t, std::int32_t> pts_;
};

}  // namespace cppgram
