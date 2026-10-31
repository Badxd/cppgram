#include <gtest/gtest.h>

#include "cppgram/mtproto/transport.h"

using namespace cppgram::mtproto;

namespace {

Bytes payload_of(std::size_t size, std::uint8_t seed) {
    Bytes out(size);
    for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>(seed + i * 31);
    return out;
}

// Feeds `wire` one byte at a time and collects every frame that comes out.
std::vector<Bytes> decode_bytewise(Transport& t, const Bytes& wire) {
    std::vector<Bytes> frames;
    for (std::uint8_t b : wire) {
        t.feed(Bytes{b});
        while (auto f = t.next()) frames.push_back(*f);
    }
    return frames;
}

}  // namespace

TEST(TransportFraming, InitBytesPerKind) {
    EXPECT_EQ(Transport(TransportKind::Abridged).init_bytes(), (Bytes{0xef}));
    EXPECT_EQ(Transport(TransportKind::Intermediate).init_bytes(), (Bytes{0xee, 0xee, 0xee, 0xee}));
    EXPECT_TRUE(Transport(TransportKind::Full).init_bytes().empty());
}

TEST(TransportFraming, RoundTripAllKinds) {
    for (TransportKind kind : {TransportKind::Abridged, TransportKind::Intermediate, TransportKind::Full}) {
        Transport enc(kind);
        Transport dec(kind);
        const Bytes a = payload_of(8, 1);
        const Bytes b = payload_of(400, 2);
        Bytes wire = enc.encode(a);
        const Bytes wire_b = enc.encode(b);
        wire.insert(wire.end(), wire_b.begin(), wire_b.end());
        const auto frames = decode_bytewise(dec, wire);
        ASSERT_EQ(frames.size(), 2u) << "kind " << static_cast<int>(kind);
        EXPECT_EQ(frames[0], a);
        EXPECT_EQ(frames[1], b);
    }
}

TEST(TransportFraming, AbridgedLongHeaderForLargePayload) {
    Transport enc(TransportKind::Abridged);
    Transport dec(TransportKind::Abridged);
    const Bytes big = payload_of(0x7f * 4 + 4, 9);  // needs the 4-byte length form
    const Bytes wire = enc.encode(big);
    EXPECT_EQ(wire[0], 0x7f);
    dec.feed(wire);
    const auto f = dec.next();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, big);
}

TEST(TransportFraming, AbridgedRejectsUnalignedPayload) {
    Transport enc(TransportKind::Abridged);
    EXPECT_THROW(enc.encode(Bytes(7, 0)), TransportError);
}

TEST(TransportFraming, IntermediateLargeFrame) {
    Transport enc(TransportKind::Intermediate);
    Transport dec(TransportKind::Intermediate);
    const Bytes big = payload_of(1024 * 1024, 5);
    dec.feed(enc.encode(big));
    const auto f = dec.next();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(*f, big);
}

TEST(TransportFraming, TruncatedFrameWaitsForMoreBytes) {
    Transport enc(TransportKind::Full);
    Transport dec(TransportKind::Full);
    const Bytes wire = enc.encode(payload_of(64, 3));
    dec.feed(Bytes(wire.begin(), wire.end() - 1));
    EXPECT_FALSE(dec.next().has_value());
    dec.feed(Bytes(wire.end() - 1, wire.end()));
    EXPECT_TRUE(dec.next().has_value());
}

TEST(TransportFraming, FullDetectsCorruptedCrc) {
    Transport enc(TransportKind::Full);
    Transport dec(TransportKind::Full);
    Bytes wire = enc.encode(payload_of(32, 4));
    wire[wire.size() - 1] ^= 0x01;
    dec.feed(wire);
    EXPECT_THROW(dec.next(), TransportError);
}

TEST(TransportFraming, FullRejectsImpossibleLength) {
    Transport dec(TransportKind::Full);
    dec.feed(Bytes{0x04, 0x00, 0x00, 0x00});  // total < 12
    EXPECT_THROW(dec.next(), TransportError);
}

TEST(TransportFraming, IntermediateRejectsNegativeServerErrorCode) {
    // The server sends -404 (0xfffffe6c) instead of a length word.
    Transport dec(TransportKind::Intermediate);
    dec.feed(Bytes{0x6c, 0xfe, 0xff, 0xff});
    EXPECT_THROW(dec.next(), TransportError);
}

TEST(TransportFraming, AbridgedRejectsOversizedFrame) {
    Transport dec(TransportKind::Abridged);
    dec.feed(Bytes{0x7f, 0xff, 0xff, 0xff});  // length 0xffffff words, way over the cap
    EXPECT_THROW(dec.next(), TransportError);
}

TEST(TransportFraming, EmptyBufferYieldsNothing) {
    Transport dec(TransportKind::Intermediate);
    EXPECT_FALSE(dec.next().has_value());
}

// F9: after a malformed frame the decoder does not advance, so the same error is raised again. Callers must
// drop the connection; this test pins that documented behaviour.
TEST(TransportFraming, MalformedIntermediateFrameKeepsThrowing) {
    Transport rx(TransportKind::Intermediate);
    // A negative length word (transport error code -404 as little-endian int32).
    const Bytes bad{0x6c, 0xfe, 0xff, 0xff};
    rx.feed(bad);
    EXPECT_THROW(rx.next(), TransportError);
    EXPECT_THROW(rx.next(), TransportError);
}
