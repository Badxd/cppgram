// cppgram - encrypted MTProto session: request/response over a PacketChannel, acks, pings,
// update queueing, and on-disk session storage.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

class RpcError : public std::runtime_error {
public:
    RpcError(std::int32_t code, const std::string& message)
        : std::runtime_error("RPC error " + std::to_string(code) + ": " + message), code_(code) {}
    std::int32_t code() const noexcept { return code_; }

private:
    std::int32_t code_;
};

// FLOOD_WAIT_<n>: the server asks us to wait n seconds before retrying.
class FloodWaitError : public RpcError {
public:
    FloodWaitError(std::int32_t code, const std::string& message, std::int32_t seconds)
        : RpcError(code, message), seconds_(seconds) {}
    std::int32_t seconds() const noexcept { return seconds_; }

private:
    std::int32_t seconds_;
};

// Throws the most specific error type for an rpc_error (FloodWaitError for FLOOD_WAIT_n).
[[noreturn]] void throw_rpc_error(std::int32_t code, const std::string& message);

inline std::uint64_t le_u64_from(const Bytes& b) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8 && i < b.size(); ++i) {
        v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    }
    return v;
}

struct SessionData {
    std::int32_t dc_id = 0;
    Bytes auth_key;  // empty when not yet created
    std::uint64_t server_salt = 0;
    std::int64_t time_offset = 0;
};

// Binary session file: magic, dc_id, time_offset, server_salt, auth_key(256).
bool load_session_file(const std::string& path, SessionData& out);
void save_session_file(const std::string& path, const SessionData& data);

class MtSession {
public:
    MtSession(PacketChannel& channel, SessionData data);

    // Sends one TL request body and returns the TL body of its result. Updates received meanwhile are queued.
    // Throws ConnectionTimeout if no matching rpc_result arrives within the request timeout, even when
    // unrelated packets keep arriving. Requests are never resent after a timeout.
    Bytes invoke(const Bytes& request_body);

    // Overall deadline for one invoke() call (default 30 s).
    void set_request_timeout(std::chrono::milliseconds timeout) noexcept { request_timeout_ = timeout; }
    std::chrono::milliseconds request_timeout() const noexcept { return request_timeout_; }

    // Typed call: serializes the request and parses its result with the generated read_result().
    template <class Request>
    typename Request::Result call(const Request& request) {
        TlWriter w;
        request.serialize(w);
        const Bytes result = invoke(w.bytes());
        TlReader r(result);
        return Request::read_result(r);
    }

    // Sends a content message without waiting for a reply (e.g. ping_delay_disconnect).
    void send_content(const Bytes& body);

    // Receives and dispatches exactly one packet. Throws ConnectionTimeout if the channel times out.
    void process_one();

    // Raw TL bodies of updates received since the last call (updates, updateShort, ...).
    std::vector<Bytes> take_updates();

    const SessionData& data() const noexcept { return data_; }

private:
    std::chrono::milliseconds request_timeout_{std::chrono::seconds(30)};

    struct Waiting {
        std::uint64_t msg_id = 0;
        bool found = false;
        bool salt_changed = false;
        Bytes result;
    };

    std::uint64_t send_encrypted(const Bytes& body, bool content);
    void send_ack(std::uint64_t msg_id);
    void handle_packet(const Bytes& packet, Waiting* waiting);
    void dispatch(const InnerMessage& inner, Waiting* waiting);
    void process_one_into(Waiting* waiting);

    PacketChannel& channel_;
    SessionData data_;
    std::uint64_t session_id_;
    std::uint32_t content_counter_ = 0;
    std::deque<Bytes> pending_updates_;
};

// Decompresses a gzip_packed payload (RFC 1952 data).
Bytes gunzip(const Bytes& data);

}  // namespace cppgram::mtproto
