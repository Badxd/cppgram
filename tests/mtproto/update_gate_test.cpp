#include <gtest/gtest.h>

#include <limits>

#include "cppgram/update_gate.h"

using cppgram::PtsDecision;
using cppgram::classify_pts;

TEST(UpdateGate, NextInSequenceIsApplied) {
    EXPECT_EQ(classify_pts(100, 101, 1), PtsDecision::Apply);
    EXPECT_EQ(classify_pts(100, 105, 5), PtsDecision::Apply);  // one update covering 5 steps
    EXPECT_EQ(classify_pts(0, 1, 1), PtsDecision::Apply);
}

TEST(UpdateGate, AlreadyAppliedIsDuplicate) {
    EXPECT_EQ(classify_pts(100, 100, 1), PtsDecision::Duplicate);  // same update seen twice
    EXPECT_EQ(classify_pts(100, 99, 1), PtsDecision::Duplicate);
    EXPECT_EQ(classify_pts(100, 101, 2), PtsDecision::Duplicate);  // expected 102, got 101: overlaps what we have
}

TEST(UpdateGate, SkippedUpdatesAreGap) {
    EXPECT_EQ(classify_pts(100, 102, 1), PtsDecision::Gap);
    EXPECT_EQ(classify_pts(100, 200, 5), PtsDecision::Gap);
    EXPECT_EQ(classify_pts(100, 103, 2), PtsDecision::Gap);  // expected 102, got 103: one step was missed
}

TEST(UpdateGate, LargePtsCountCannotOverflow) {
    const std::int32_t max = std::numeric_limits<std::int32_t>::max();
    // local + count would overflow 32 bits; the 64-bit check must still classify correctly.
    EXPECT_EQ(classify_pts(max, max, max), PtsDecision::Duplicate);
    EXPECT_EQ(classify_pts(0, max, max), PtsDecision::Apply);
    EXPECT_EQ(classify_pts(1, max, max), PtsDecision::Duplicate);
}
