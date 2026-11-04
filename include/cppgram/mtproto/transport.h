// cppgram - MTProto transport framing: abridged, intermediate, full.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

class TransportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class TransportKind { Abridged, Intermediate, Full };

// Pure framing codec: no sockets. Feed received bytes, pull complete frames.
class Transport {
public:
    explicit Transport(TransportKind kind) noexcept : kind_(kind) {}

    TransportKind kind() const noexcept { return kind_; }

    // Bytes that must be sent once, right after connecting.
    Bytes init_bytes() const;
    // Wraps one payload into a frame ready to be written to the socket.
    Bytes encode(const Bytes& payload);
    // Appends received bytes to the internal buffer.
    void feed(const Bytes& data);
    // Returns the next complete payload, or nullopt if more bytes are needed.
    // Throws TransportError on a malformed or oversized frame (for example a negative length word in
    // intermediate mode). The read position does not advance on error, so every later call throws again
    // for the same bytes. The caller must close the connection and create a new Transport.
    // encode() throws TransportError for an abridged payload whose size is not a multiple of 4.
    std::optional<Bytes> next();

private:
    TransportKind kind_;
    std::uint32_t send_seq_ = 0;
    Bytes buffer_;
    std::size_t consumed_ = 0;
};

}  // namespace cppgram::mtproto
