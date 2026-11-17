#include "cppgram/channel_pts.h"

namespace cppgram {

void ChannelPtsTracker::set_baseline(std::int64_t channel_id, std::int32_t pts) {
    pts_[channel_id] = pts;
}

void ChannelPtsTracker::forget(std::int64_t channel_id) {
    pts_.erase(channel_id);
}

std::optional<std::int32_t> ChannelPtsTracker::pts(std::int64_t channel_id) const {
    const auto it = pts_.find(channel_id);
    if (it == pts_.end()) return std::nullopt;
    return it->second;
}

PtsDecision ChannelPtsTracker::classify(std::int64_t channel_id, std::int32_t pts, std::int32_t pts_count) const {
    const auto local = this->pts(channel_id);
    if (!local) return PtsDecision::Gap;
    return classify_pts(*local, pts, pts_count);
}

void ChannelPtsTracker::mark_applied(std::int64_t channel_id, std::int32_t pts) {
    pts_[channel_id] = pts;
}

}  // namespace cppgram
