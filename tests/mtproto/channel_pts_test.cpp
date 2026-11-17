#include <gtest/gtest.h>

#include "cppgram/channel_pts.h"

using namespace cppgram;

TEST(ChannelPts, UnknownChannelNeedsDifference) {
    ChannelPtsTracker t;
    EXPECT_EQ(t.classify(100, 5, 1), PtsDecision::Gap);
    EXPECT_FALSE(t.pts(100).has_value());
}

TEST(ChannelPts, ChannelsKeepSeparateSequences) {
    ChannelPtsTracker t;
    t.set_baseline(100, 10);
    t.set_baseline(200, 50);
    EXPECT_EQ(t.classify(100, 11, 1), PtsDecision::Apply);
    EXPECT_EQ(t.classify(200, 11, 1), PtsDecision::Duplicate);  // 11 is far behind channel 200
    EXPECT_EQ(t.classify(200, 51, 1), PtsDecision::Apply);
}

TEST(ChannelPts, DuplicateAndGapAreDetectedPerChannel) {
    ChannelPtsTracker t;
    t.set_baseline(7, 20);
    EXPECT_EQ(t.classify(7, 20, 1), PtsDecision::Duplicate);
    EXPECT_EQ(t.classify(7, 25, 1), PtsDecision::Gap);
}

TEST(ChannelPts, MarkAppliedAdvancesOnlyThatChannel) {
    ChannelPtsTracker t;
    t.set_baseline(1, 0);
    t.set_baseline(2, 0);
    t.mark_applied(1, 3);
    EXPECT_EQ(*t.pts(1), 3);
    EXPECT_EQ(*t.pts(2), 0);
}

TEST(ChannelPts, ForgetReturnsChannelToGap) {
    ChannelPtsTracker t;
    t.set_baseline(9, 4);
    t.forget(9);
    EXPECT_EQ(t.classify(9, 5, 1), PtsDecision::Gap);
}
