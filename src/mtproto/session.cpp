#include <cctype>
#include "cppgram/mtproto/session.h"

#include <zlib.h>

#include <cstring>
#include <fstream>

#include "cppgram/mtproto/schema/api.h"
#include "../secure_file.h"

namespace cppgram::mtproto {

namespace {

namespace ty = tl::types;

constexpr std::uint32_t kGzipPackedId = 0x3072cfa1u;
constexpr std::uint32_t kMsgContainerId = 0x73f1f8dcu;
constexpr std::uint32_t kPingId = 0x7abe77ecu;
constexpr char kSessionMagic[4] = {'C', 'G', 'S', '1'};

bool is_updates_id(std::uint32_t id) {
    switch (id) {
        case ty::updates::kId:
        case ty::updateShort::kId:
        case ty::updatesCombined::kId:
        case ty::updateShortMessage::kId:
        case ty::updateShortChatMessage::kId:
        case ty::updateShortSentMessage::kId:
        case ty::updatesTooLong::kId:
            return true;
        default:
            return false;
    }
}

void put_u64(std::ostream& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.put(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

bool get_u64(std::istream& in, std::uint64_t& v) {
    v = 0;
    for (int i = 0; i < 8; ++i) {
        const int c = in.get();
        if (c == std::char_traits<char>::eof()) return false;
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(c)) << (8 * i);
    }
    return true;
}

// Unpacks the body of rpc_result: gzip_packed is inflated, rpc_error becomes an exception.
Bytes unpack_rpc_payload(const Bytes& payload) {
    if (payload.size() < 4) {
        throw RpcError(0, "short rpc payload");
    }
    TlReader r(payload);
    const std::uint32_t id = r.read_uint32();
    if (id == ty::rpc_error::kId) {
        const ty::rpc_error err = ty::rpc_error::read_body(r);
        throw_rpc_error(err.error_code, err.error_message);
    }
    if (id == kGzipPackedId) {
        return unpack_rpc_payload(gunzip(r.read_bytes()));
    }
    return payload;
}

}  // namespace

void throw_rpc_error(std::int32_t code, const std::string& message) {
    const std::string prefix = "FLOOD_WAIT_";
    if (message.compare(0, prefix.size(), prefix) == 0) {
        std::int32_t seconds = 0;
        for (std::size_t i = prefix.size(); i < message.size() && std::isdigit(static_cast<unsigned char>(message[i])); ++i) {
            seconds = seconds * 10 + (message[i] - '0');
        }
        throw FloodWaitError(code, message, seconds);
    }
    throw RpcError(code, message);
}


bool load_session_file(const std::string& path, SessionData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kSessionMagic, 4) != 0) {
        return false;
    }
    std::uint64_t dc = 0;
    std::uint64_t offset = 0;
    std::uint64_t salt = 0;
    if (!get_u64(in, dc) || !get_u64(in, offset) || !get_u64(in, salt)) {
        return false;
    }
    Bytes key(256);
    in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
    if (!in) {
        return false;
    }
    out.dc_id = static_cast<std::int32_t>(dc);
    out.time_offset = static_cast<std::int64_t>(offset);
    out.server_salt = salt;
    out.auth_key = std::move(key);
    return true;
}

void save_session_file(const std::string& path, const SessionData& data) {
    if (data.auth_key.size() != 256) {
        throw std::runtime_error("cannot save session without a 256-byte auth key");
    }
    detail::ensure_private_file(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open session file for writing: " + path);
    }
    out.write(kSessionMagic, 4);
    put_u64(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(data.dc_id)));
    put_u64(out, static_cast<std::uint64_t>(data.time_offset));
    put_u64(out, data.server_salt);
    out.write(reinterpret_cast<const char*>(data.auth_key.data()), static_cast<std::streamsize>(data.auth_key.size()));
    if (!out) {
        throw std::runtime_error("failed to write session file: " + path);
    }
}

Bytes gunzip(const Bytes& data) {
    z_stream s{};
    if (inflateInit2(&s, 16 + MAX_WBITS) != Z_OK) {
        throw CryptoError("inflateInit2 failed");
    }
    s.next_in = const_cast<Bytef*>(data.data());
    s.avail_in = static_cast<uInt>(data.size());
    Bytes out;
    unsigned char buf[16384];
    int ret = Z_OK;
    do {
        s.next_out = buf;
        s.avail_out = sizeof(buf);
        ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&s);
            throw CryptoError("gzip inflate failed");
        }
        out.insert(out.end(), buf, buf + (sizeof(buf) - s.avail_out));
    } while (ret != Z_STREAM_END && !(s.avail_in == 0 && s.avail_out != 0));
    inflateEnd(&s);
    if (ret != Z_STREAM_END) {
        throw CryptoError("truncated gzip data");
    }
    return out;
}

MtSession::MtSession(PacketChannel& channel, SessionData data)
    : channel_(channel), data_(std::move(data)), session_id_(le_u64_from(random_bytes(8))) {}

std::uint64_t MtSession::send_encrypted(const Bytes& body, bool content) {
    const std::uint64_t msg_id = make_message_id(data_.time_offset);
    InnerMessage inner;
    inner.salt = data_.server_salt;
    inner.session_id = session_id_;
    inner.message_id = msg_id;
    // Content messages carry odd seqno (2n+1); service messages carry the current even value.
    inner.seq_no = content ? static_cast<std::uint32_t>(2 * content_counter_++ + 1)
                           : static_cast<std::uint32_t>(2 * content_counter_);
    inner.body = body;
    channel_.send_packet(encrypt_packet(data_.auth_key, serialize_inner(inner), true));
    return msg_id;
}

void MtSession::send_ack(std::uint64_t msg_id) {
    ty::msgs_ack ack;
    ack.msg_ids = {static_cast<std::int64_t>(msg_id)};
    TlWriter w;
    ack.serialize(w);
    send_encrypted(w.bytes(), false);
}

void MtSession::send_content(const Bytes& body) {
    send_encrypted(body, true);
}

Bytes MtSession::invoke(const Bytes& request_body) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t msg_id = send_encrypted(request_body, true);
        Waiting waiting;
        waiting.msg_id = msg_id;
        while (!waiting.found && !waiting.salt_changed) {
            process_one_into(&waiting);
        }
        if (waiting.found) {
            return waiting.result;
        }
    }
    throw std::runtime_error("server kept rejecting the request after salt updates");
}

void MtSession::process_one() {
    process_one_into(nullptr);
}

void MtSession::process_one_into(Waiting* waiting) {
    const Bytes packet = channel_.receive_packet();
    const Bytes plain = decrypt_packet(data_.auth_key, packet, false);
    dispatch(parse_inner(plain), waiting);
}

std::vector<Bytes> MtSession::take_updates() {
    std::vector<Bytes> out(pending_updates_.begin(), pending_updates_.end());
    pending_updates_.clear();
    return out;
}

void MtSession::dispatch(const InnerMessage& inner, Waiting* waiting) {
    // Server content messages must be acknowledged, otherwise the server resends them.
    if ((inner.seq_no & 1u) != 0) {
        send_ack(inner.message_id);
    }
    if (inner.body.size() < 4) {
        return;
    }
    TlReader r(inner.body);
    const std::uint32_t id = r.read_uint32();
    switch (id) {
        case ty::rpc_result::kId: {
            const std::uint64_t req_msg_id = r.read_uint64();
            Bytes payload = r.read_raw(r.remaining());
            if (waiting != nullptr && req_msg_id == waiting->msg_id) {
                waiting->found = true;
                waiting->result = unpack_rpc_payload(payload);
            }
            return;
        }
        case ty::bad_server_salt::kId: {
            const ty::bad_server_salt bad = ty::bad_server_salt::read_body(r);
            data_.server_salt = static_cast<std::uint64_t>(bad.new_server_salt);
            if (waiting != nullptr && bad.bad_msg_id == static_cast<std::int64_t>(waiting->msg_id)) {
                waiting->salt_changed = true;
            }
            return;
        }
        case ty::bad_msg_notification::kId: {
            const ty::bad_msg_notification bad = ty::bad_msg_notification::read_body(r);
            if (waiting != nullptr && bad.bad_msg_id == static_cast<std::int64_t>(waiting->msg_id)) {
                throw RpcError(bad.error_code, "bad_msg_notification");
            }
            return;
        }
        case ty::new_session_created::kId: {
            const ty::new_session_created created = ty::new_session_created::read_body(r);
            data_.server_salt = static_cast<std::uint64_t>(created.server_salt);
            return;
        }
        case kPingId: {
            ty::pong pong;
            pong.msg_id = static_cast<std::int64_t>(inner.message_id);
            pong.ping_id = r.read_int64();
            TlWriter w;
            pong.serialize(w);
            send_encrypted(w.bytes(), false);
            return;
        }
        case kMsgContainerId: {
            const std::uint32_t count = r.read_uint32();
            for (std::uint32_t i = 0; i < count; ++i) {
                InnerMessage sub;
                sub.salt = inner.salt;
                sub.session_id = inner.session_id;
                sub.message_id = r.read_uint64();
                sub.seq_no = r.read_uint32();
                const std::uint32_t length = r.read_uint32();
                sub.body = r.read_raw(length);
                dispatch(sub, waiting);
            }
            return;
        }
        case kGzipPackedId: {
            InnerMessage sub = inner;
            sub.body = gunzip(r.read_bytes());
            sub.seq_no = 0;  // already acknowledged above
            dispatch(sub, waiting);
            return;
        }
        default:
            if (is_updates_id(id)) {
                pending_updates_.push_back(inner.body);
            }
            return;
    }
}

}  // namespace cppgram::mtproto
