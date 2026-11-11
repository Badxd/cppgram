// Proxy and transport audit tests.
//
//  * Known-answer vectors. The handshake, the encrypted frame and the server reply bytes come from kat2/ref_mtproxy.py,
//    a separate implementation written from the published layout rules. cppgram did not produce them.
//  * Loopback proxy stub. A second MTProxy server, written directly on OpenSSL and POSIX sockets. It derives its own keys
//    and calls no cppgram crypto or framing code. The real Connection talks to it.
//  * SOCKS5 stub. Fragmented and malformed replies.
//
// Offline only: 127.0.0.1 and fixed test bytes. No Telegram server, no credentials, no live network.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/socks5.h"
#include "cppgram/mtproto/transport.h"

using namespace cppgram::mtproto;

namespace {

// ---------- vectors from kat2/ref_mtproxy.py -------------------------------------------------------------------------

struct KatCase {
    const char* framing;
    int dc;
    const char* secret_hex;
    const char* init_hex;
    const char* handshake_hex;
    const char* wire_frame_hex;
    const char* server_wire_hex;
    const char* server_plain_hex;
};

const KatCase kIntermediate2 { "intermediate",
                               2,
                               "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                               "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2"
                               "b2c2d2e2f3031323334353637eeeeeeee02003e3f",
                               "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2"
                               "b2c2d2e2f3031323334353637e0e35e3d7ae555a9",
                               "bd2e25178cecd289874c291c",
                               "439bed23d9c7850fe823bfe1",
                               "080000003132333435363738" };
const KatCase kPadded2 { "padded",
                         2,
                         "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                         "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2"
                         "e2f3031323334353637dddddddd02003e3f",
                         "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2"
                         "e2f3031323334353637d3d06d0e7ae555a9",
                         "b82e25178cecd289874c291c142232c152",
                         "439bed23d9c7850fe823bfe1",
                         "080000003132333435363738" };
const KatCase kAbridged2 { "abridged",
                           2,
                           "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                           "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2"
                           "d2e2f3031323334353637efefefef02003e3f",
                           "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2"
                           "d2e2f3031323334353637e1e25f3c7ae555a9",
                           "b76f675489ebd78a8a",
                           "439bed23d9c7850fe823bfe1",
                           "080000003132333435363738" };
const KatCase kIntermediateMedia { "intermediate",
                                   -2,
                                   "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                                   "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627282"
                                   "92a2b2c2d2e2f3031323334353637eeeeeeeefeff3e3f",
                                   "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627282"
                                   "92a2b2c2d2e2f3031323334353637e0e35e3d861a55a9",
                                   "bd2e25178cecd289874c291c",
                                   "439bed23d9c7850fe823bfe1",
                                   "080000003132333435363738" };
const KatCase kIntermediateTest { "intermediate",
                                  10002,
                                  "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                                  "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526272829"
                                  "2a2b2c2d2e2f3031323334353637eeeeeeee12273e3f",
                                  "000102031122334408090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526272829"
                                  "2a2b2c2d2e2f3031323334353637e0e35e3d6ac255a9",
                                  "bd2e25178cecd289874c291c",
                                  "439bed23d9c7850fe823bfe1",
                                  "080000003132333435363738" };

const KatCase* const kAllKat[] = { &kIntermediate2, &kPadded2, &kAbridged2, &kIntermediateMedia, &kIntermediateTest };

// ---------- helpers ---------------------------------------------------------------------------------------------------

Bytes from_hex(const std::string& hex) {
    Bytes out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::string to_hex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}

std::array<std::uint8_t, 16> secret_array(const Bytes& b) {
    std::array<std::uint8_t, 16> out {};
    std::copy(b.begin(), b.begin() + 16, out.begin());
    return out;
}

ObfuscatedFraming framing_named(const std::string& name) {
    if (name == "padded")
        return ObfuscatedFraming::PaddedIntermediate;
    if (name == "abridged")
        return ObfuscatedFraming::Abridged;
    return ObfuscatedFraming::Intermediate;
}

// The plain frame the vector was made from, for each framing, with the same payload "ABCDEFGH".
Bytes plain_frame_for(const std::string& name) {
    const Bytes payload = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H' };
    Bytes out;
    if (name == "abridged") {
        out.push_back(0x02);
    } else if (name == "padded") {
        out = { 0x0d, 0x00, 0x00, 0x00 }; // 8 payload bytes + 5 padding bytes
    } else {
        out = { 0x08, 0x00, 0x00, 0x00 };
    }
    out.insert(out.end(), payload.begin(), payload.end());
    if (name == "padded") {
        const Bytes pad = { 0x5a, 0xa5, 0x00, 0x0f, 0x11 };
        out.insert(out.end(), pad.begin(), pad.end());
    }
    return out;
}

// ---------- independent MTProxy stub -----------------------------------------------------------------------------------

class Fd {
public:
    explicit Fd(int fd = -1)
        : fd_(fd) {
    }
    ~Fd() {
        if (fd_ >= 0)
            ::close(fd_);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept
        : fd_(o.fd_) {
        o.fd_ = -1;
    }
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) {
            if (fd_ >= 0)
                ::close(fd_);
            fd_ = o.fd_;
            o.fd_ = -1;
        }
        return *this;
    }
    int get() const {
        return fd_;
    }
    int release() {
        const int f = fd_;
        fd_ = -1;
        return f;
    }

private:
    int fd_;
};

// AES-256-CTR keystream, written on OpenSSL for the stub. Not cppgram's CtrStream.
class StubCtr {
public:
    StubCtr(const Bytes& key, const Bytes& iv)
        : ctx_(EVP_CIPHER_CTX_new()) {
        if (ctx_ == nullptr || EVP_CipherInit_ex(ctx_, EVP_aes_256_ctr(), nullptr, key.data(), iv.data(), 1) != 1) {
            throw std::runtime_error("stub AES init failed");
        }
    }
    ~StubCtr() {
        EVP_CIPHER_CTX_free(ctx_);
    }
    StubCtr(const StubCtr&) = delete;
    StubCtr& operator=(const StubCtr&) = delete;
    Bytes apply(const Bytes& in) {
        Bytes out(in.size());
        int len = 0;
        if (!in.empty()
            && (EVP_CipherUpdate(ctx_, out.data(), &len, in.data(), static_cast<int>(in.size())) != 1
                || static_cast<std::size_t>(len) != in.size())) {
            throw std::runtime_error("stub AES update failed");
        }
        return out;
    }

private:
    EVP_CIPHER_CTX* ctx_;
};

Bytes sha256_of(const Bytes& data) {
    Bytes out(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), out.data());
    return out;
}

Bytes cat(const Bytes& a, const Bytes& b) {
    Bytes out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

// Reads exactly n bytes. False on close or timeout.
bool read_exact(int fd, std::uint8_t* out, std::size_t n, int timeout_ms = 5000) {
    std::size_t got = 0;
    while (got < n) {
        pollfd p { fd, POLLIN, 0 };
        if (::poll(&p, 1, timeout_ms) <= 0)
            return false;
        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r <= 0)
            return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

void write_all(int fd, const Bytes& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t r = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (r < 0)
            throw std::runtime_error("stub send failed");
        sent += static_cast<std::size_t>(r);
    }
}

// A listening socket on 127.0.0.1 with an ephemeral port.
Fd listen_loopback(std::uint16_t& port_out) {
    Fd fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0)
        throw std::runtime_error("socket failed");
    int one = 1;
    ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd.get(), 4) != 0) {
        throw std::runtime_error("bind/listen failed");
    }
    socklen_t len = sizeof addr;
    ::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    port_out = ntohs(addr.sin_port);
    return fd;
}

enum class StubMode {
    Valid, // accepts the handshake, decodes frames, replies with one frame
    FragmentedReply, // as Valid, but the reply goes out one byte per send
    GarbageReply, // as Valid, but the reply's length word is -404 (a transport error code)
    Silent, // accepts the handshake, sends nothing, waits for the client to close
    CloseAfterHandshake, // reads the handshake and closes
    PaddedReply // as Valid, but a padded-intermediate reply carries 5 trailing padding bytes inside its frame
};

enum class StubFraming { Intermediate, Padded, Abridged, Unknown };

StubFraming framing_of_tag(const std::uint8_t tag[4]) {
    if (std::memcmp(tag, "\xee\xee\xee\xee", 4) == 0)
        return StubFraming::Intermediate;
    if (std::memcmp(tag, "\xdd\xdd\xdd\xdd", 4) == 0)
        return StubFraming::Padded;
    if (std::memcmp(tag, "\xef\xef\xef\xef", 4) == 0)
        return StubFraming::Abridged;
    return StubFraming::Unknown;
}

struct StubResult {
    bool handshake_ok = false;
    std::uint8_t tag[4] = { 0, 0, 0, 0 };
    std::int16_t dc = 0;
    std::size_t frames = 0;
    Bytes first_payload; // payload of the first frame; padded frames keep their padding
    std::size_t first_length_word = 0; // length of the first frame as the client wrote it
};

// Pulls one complete frame off the front of buf. Returns (payload, length word).
std::optional<std::pair<Bytes, std::size_t>> take_frame(Bytes& buf, StubFraming framing) {
    if (framing == StubFraming::Abridged) {
        if (buf.empty())
            return std::nullopt;
        std::size_t header = 1;
        std::size_t words = buf[0];
        if (words == 0x7f) {
            if (buf.size() < 4)
                return std::nullopt;
            header = 4;
            words = static_cast<std::size_t>(buf[1]) | (static_cast<std::size_t>(buf[2]) << 8)
                | (static_cast<std::size_t>(buf[3]) << 16);
        }
        if (buf.size() < header + words * 4)
            return std::nullopt;
        Bytes payload(buf.begin() + static_cast<std::ptrdiff_t>(header),
                      buf.begin() + static_cast<std::ptrdiff_t>(header + words * 4));
        buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(header + words * 4));
        return std::make_pair(payload, words * 4);
    }
    if (buf.size() < 4)
        return std::nullopt;
    const std::size_t len = static_cast<std::size_t>(buf[0]) | (static_cast<std::size_t>(buf[1]) << 8)
        | (static_cast<std::size_t>(buf[2]) << 16) | (static_cast<std::size_t>(buf[3]) << 24);
    if (buf.size() < 4 + len)
        return std::nullopt;
    Bytes payload(buf.begin() + 4, buf.begin() + static_cast<std::ptrdiff_t>(4 + len));
    buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(4 + len));
    return std::make_pair(payload, len);
}

class StubProxy {
public:
    StubProxy(const std::array<std::uint8_t, 16>& secret, StubMode mode)
        : secret_(secret)
        , mode_(mode) {
        listen_ = listen_loopback(port_);
        thread_ = std::thread([this] {
            try {
                serve();
            } catch (const std::exception&) {
                // a failed stub write ends this connection only; the test sees it as a missing reply
            }
        });
    }
    ~StubProxy() {
        if (thread_.joinable())
            thread_.join();
    }
    StubProxy(const StubProxy&) = delete;
    StubProxy& operator=(const StubProxy&) = delete;

    std::uint16_t port() const {
        return port_;
    }
    void join() {
        if (thread_.joinable())
            thread_.join();
    }
    StubResult result() {
        std::lock_guard<std::mutex> lock(mu_);
        return result_;
    }

private:
    Bytes secret_bytes() const {
        return Bytes(secret_.begin(), secret_.end());
    }

    void serve() {
        pollfd p { listen_.get(), POLLIN, 0 };
        if (::poll(&p, 1, 5000) <= 0)
            return;
        Fd conn(::accept4(listen_.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (conn.get() < 0)
            return;
        int one = 1;
        ::setsockopt(conn.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

        Bytes init(64);
        if (!read_exact(conn.get(), init.data(), 64))
            return;
        if (mode_ == StubMode::CloseAfterHandshake)
            return;

        // The client-to-proxy stream: key SHA256(init[8:40] || secret), iv init[40:56]. The 64 handshake bytes advance it.
        // Only bytes 56..64 were encrypted, so the tag and the DC field are recovered from that tail.
        StubCtr c2s(sha256_of(cat(Bytes(init.begin() + 8, init.begin() + 40), secret_bytes())),
                    Bytes(init.begin() + 40, init.begin() + 56));
        const Bytes keystream = c2s.apply(Bytes(64, 0));
        Bytes tail(8);
        for (std::size_t i = 0; i < 8; ++i)
            tail[i] = static_cast<std::uint8_t>(init[56 + i] ^ keystream[56 + i]);
        StubResult local;
        std::memcpy(local.tag, tail.data(), 4);
        local.dc = static_cast<std::int16_t>(static_cast<std::uint16_t>(tail[4] | (tail[5] << 8)));
        const StubFraming framing = framing_of_tag(local.tag);
        if (framing == StubFraming::Unknown)
            return; // a wrong secret decrypts the tag to noise: close

        Bytes rev(init.rbegin(), init.rend());
        StubCtr s2c(sha256_of(cat(Bytes(rev.begin() + 8, rev.begin() + 40), secret_bytes())),
                    Bytes(rev.begin() + 40, rev.begin() + 56));
        local.handshake_ok = true;
        {
            std::lock_guard<std::mutex> lock(mu_);
            result_ = local;
        }
        if (mode_ == StubMode::Silent) {
            std::uint8_t sink[256];
            for (;;) {
                pollfd q { conn.get(), POLLIN, 0 };
                if (::poll(&q, 1, 5000) <= 0)
                    return;
                if (::recv(conn.get(), sink, sizeof sink, 0) <= 0)
                    return;
            }
        }

        Bytes buf;
        bool replied = false;
        for (;;) {
            pollfd q { conn.get(), POLLIN, 0 };
            if (::poll(&q, 1, 5000) <= 0)
                return;
            std::uint8_t chunk[4096];
            const ssize_t r = ::recv(conn.get(), chunk, sizeof chunk, 0);
            if (r <= 0)
                return;
            const Bytes plain = c2s.apply(Bytes(chunk, chunk + r));
            buf.insert(buf.end(), plain.begin(), plain.end());
            while (auto frame = take_frame(buf, framing)) {
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    if (result_.frames == 0) {
                        result_.first_payload = frame->first;
                        result_.first_length_word = frame->second;
                    }
                    result_.frames++;
                }
                if (!replied) {
                    replied = true;
                    send_reply(conn.get(), s2c, framing);
                }
            }
        }
    }

    void send_reply(int fd, StubCtr& s2c, StubFraming framing) {
        const std::string text = "PROXY-REPLY-0123456789abcdefghij";
        const Bytes reply(text.begin(), text.end());
        Bytes frame;
        if (mode_ == StubMode::GarbageReply) {
            frame = { 0x6c, 0xfe, 0xff, 0xff }; // -404 as a little-endian int32
        } else if (framing == StubFraming::Abridged) {
            frame.push_back(static_cast<std::uint8_t>(reply.size() / 4));
            frame.insert(frame.end(), reply.begin(), reply.end());
        } else {
            Bytes body = reply;
            if (mode_ == StubMode::PaddedReply)
                body.insert(body.end(), 5, 0x5a);
            for (int i = 0; i < 4; ++i)
                frame.push_back(static_cast<std::uint8_t>(body.size() >> (8 * i)));
            frame.insert(frame.end(), body.begin(), body.end());
        }
        const Bytes wire = s2c.apply(frame);
        if (mode_ == StubMode::FragmentedReply) {
            for (std::uint8_t b : wire) {
                write_all(fd, Bytes { b });
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            write_all(fd, wire);
        }
    }

    std::array<std::uint8_t, 16> secret_;
    StubMode mode_;
    Fd listen_;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::mutex mu_;
    StubResult result_;
};

// ---------- SOCKS5 stub ------------------------------------------------------------------------------------------------

enum class Socks5Stub { FragmentedOk, WrongVersionInGreeting, WrongVersionInReply, UnknownAddressTypeInReply, Refused };

void serve_socks5(int listen_fd, Socks5Stub mode) {
    pollfd p { listen_fd, POLLIN, 0 };
    if (::poll(&p, 1, 5000) <= 0)
        return;
    Fd conn(::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC));
    if (conn.get() < 0)
        return;
    std::uint8_t head[2];
    if (!read_exact(conn.get(), head, 2))
        return;
    Bytes methods(head[1]);
    if (!read_exact(conn.get(), methods.data(), methods.size()))
        return;
    if (mode == Socks5Stub::WrongVersionInGreeting) {
        write_all(conn.get(), { 0x04, 0x00 });
        return;
    }
    if (mode == Socks5Stub::FragmentedOk) {
        write_all(conn.get(), { 0x05 }); // the method reply comes in pieces
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        write_all(conn.get(), { 0x00 });
    } else {
        write_all(conn.get(), { 0x05, 0x00 });
    }
    std::uint8_t req[4];
    if (!read_exact(conn.get(), req, 4))
        return;
    std::size_t rest = 0;
    if (req[3] == 0x01)
        rest = 4 + 2;
    else if (req[3] == 0x04)
        rest = 16 + 2;
    else if (req[3] == 0x03) {
        std::uint8_t len;
        if (!read_exact(conn.get(), &len, 1))
            return;
        rest = len + 2;
    }
    Bytes skip(rest);
    if (!read_exact(conn.get(), skip.data(), skip.size()))
        return;

    if (mode == Socks5Stub::Refused) {
        write_all(conn.get(), { 0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0 }); // REP 5: connection refused
        return;
    }
    if (mode == Socks5Stub::WrongVersionInReply) {
        write_all(conn.get(), { 0x04, 0x00, 0x00, 0x01, 127, 0, 0, 1, 0, 0 });
        return;
    }
    if (mode == Socks5Stub::UnknownAddressTypeInReply) {
        write_all(conn.get(), { 0x05, 0x00, 0x00, 0x09, 0, 0, 0, 0, 0, 0 });
        return;
    }
    // Success: the reply is sent in three pieces.
    write_all(conn.get(), { 0x05, 0x00 });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    write_all(conn.get(), { 0x00, 0x01, 127, 0 });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    write_all(conn.get(), { 0, 1, 0x01, 0xbb });
    std::uint8_t sink[64];
    pollfd q { conn.get(), POLLIN, 0 };
    if (::poll(&q, 1, 2000) > 0)
        ::recv(conn.get(), sink, sizeof sink, 0);
}

// ---------- shared fixtures ------------------------------------------------------------------------------------------

const char* const kSecretHex = "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
// 32 bytes, so the abridged frame (word count) carries the whole reply.
const std::string kReplyText = "PROXY-REPLY-0123456789abcdefghij";

Bytes text(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

ProxySecret plain_secret() {
    ProxySecret s;
    s.kind = ProxySecretKind::Plain;
    s.key = secret_array(from_hex(kSecretHex));
    return s;
}

ProxySecret dd_secret() {
    ProxySecret s = plain_secret();
    s.kind = ProxySecretKind::PaddedIntermediate;
    return s;
}

ProxySecret wrong_secret() {
    ProxySecret s = plain_secret();
    s.key[0] ^= 0xff;
    return s;
}

std::array<std::uint8_t, 16> stub_key() {
    return secret_array(from_hex(kSecretHex));
}

// Socks5 stub on a loopback port. The server thread is joined in the destructor.
struct Socks5Server {
    Fd fd;
    std::uint16_t port = 0;
    std::thread th;
    explicit Socks5Server(Socks5Stub mode) {
        fd = listen_loopback(port);
        th = std::thread([this, mode] {
            try {
                serve_socks5(fd.get(), mode);
            } catch (const std::exception&) { }
        });
    }
    ~Socks5Server() {
        if (th.joinable())
            th.join();
    }
};

Socks5Options socks5_options(std::uint16_t port) {
    Socks5Options o;
    o.proxy_host = "127.0.0.1";
    o.proxy_port = port;
    o.timeout = std::chrono::milliseconds(3000);
    return o;
}

// ---------- known-answer vectors -------------------------------------------------------------------------------------

TEST(ProxyKnownAnswer, MatchesIndependentReferenceForEveryCase) {
    for (const KatCase* c : kAllKat) {
        SCOPED_TRACE(std::string(c->framing) + " dc " + std::to_string(c->dc));
        const ObfuscatedFraming framing = framing_named(c->framing);
        auto client = ObfuscatedClient::create(framing, static_cast<std::int16_t>(c->dc),
                                               secret_array(from_hex(c->secret_hex)), from_hex(c->init_hex));
        EXPECT_EQ(to_hex(client.handshake()), std::string(c->handshake_hex));
        const Bytes plain = plain_frame_for(c->framing);
        if (framing != ObfuscatedFraming::PaddedIntermediate) {
            // The reference frame has no padding, so the framing output must match it exactly.
            EXPECT_EQ(to_hex(client.frame(text("ABCDEFGH"))), to_hex(plain));
        }
        EXPECT_EQ(to_hex(client.encrypt(plain)), std::string(c->wire_frame_hex));
        EXPECT_EQ(to_hex(client.decrypt(from_hex(c->server_wire_hex))), std::string(c->server_plain_hex));
    }
}

// ---------- framing rules --------------------------------------------------------------------------------------------

TEST(ObfuscatedAbridged, FrameLengthEncoding) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Abridged, 2, stub_key());
    EXPECT_EQ(to_hex(client.frame(text("ABCD"))), std::string("01") + to_hex(text("ABCD")));

    // 126 words still fits the short form: one byte, 0x7e.
    const Bytes p126(126 * 4, 0x44);
    const Bytes f126 = client.frame(p126);
    ASSERT_EQ(f126.size(), 1 + p126.size());
    EXPECT_EQ(f126[0], 0x7e);

    // 127 words is the first length that needs the 0x7f escape: 0x7f, then 3 bytes little-endian.
    const Bytes p127(127 * 4, 0x55);
    const Bytes f127 = client.frame(p127);
    ASSERT_EQ(f127.size(), 4 + p127.size());
    EXPECT_EQ(to_hex(Bytes(f127.begin(), f127.begin() + 4)), std::string("7f7f0000"));

    // 0x1234 words: 0x7f, 0x34, 0x12, 0x00.
    const Bytes big(0x1234 * 4, 0x66);
    const Bytes fbig = client.frame(big);
    ASSERT_EQ(fbig.size(), 4 + big.size());
    EXPECT_EQ(to_hex(Bytes(fbig.begin(), fbig.begin() + 4)), std::string("7f341200"));
}

TEST(ObfuscatedAbridged, RejectsPayloadNotAMultipleOfFour) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::Abridged, 2, stub_key());
    EXPECT_THROW((void)client.frame(text("ABC")), std::invalid_argument);
}

TEST(ObfuscatedPadded, PaddingIsZeroToFifteenBytesAndRandom) {
    auto client = ObfuscatedClient::create(ObfuscatedFraming::PaddedIntermediate, 2, stub_key());
    const Bytes payload = text("ABCDEFGH");
    std::set<std::size_t> pads;
    for (int i = 0; i < 200; ++i) {
        const Bytes f = client.frame(payload);
        ASSERT_GE(f.size(), 12u);
        const std::size_t len = static_cast<std::size_t>(f[0]) | (static_cast<std::size_t>(f[1]) << 8)
            | (static_cast<std::size_t>(f[2]) << 16) | (static_cast<std::size_t>(f[3]) << 24);
        ASSERT_EQ(f.size(), 4 + len);
        ASSERT_GE(len, payload.size());
        ASSERT_LE(len - payload.size(), 15u);
        EXPECT_TRUE(std::equal(payload.begin(), payload.end(), f.begin() + 4));
        pads.insert(len - payload.size());
    }
    EXPECT_GT(pads.size(), 1u); // padding is random, so 200 frames should not all have the same length
}

// A framing that does not match the secret must be refused before any socket is opened. Port 9 is never contacted.
TEST(ObfuscatedFramingGuard, MismatchedFramingIsRejectedBeforeNetwork) {
    Connection c;
    const auto timeout = std::chrono::milliseconds(1000);
    EXPECT_THROW(
        c.connect_obfuscated("127.0.0.1", 9, plain_secret(), 2, timeout, ObfuscatedFraming::PaddedIntermediate),
        std::invalid_argument);
    EXPECT_THROW(c.connect_obfuscated("127.0.0.1", 9, dd_secret(), 2, timeout, ObfuscatedFraming::Intermediate),
                 std::invalid_argument);
    EXPECT_THROW(c.connect_obfuscated("127.0.0.1", 9, dd_secret(), 2, timeout, ObfuscatedFraming::Abridged),
                 std::invalid_argument);
    ProxySecret tls;
    tls.kind = ProxySecretKind::FakeTls;
    tls.fake_tls_domain = "example.com";
    EXPECT_THROW(c.connect_obfuscated("127.0.0.1", 9, tls, 2, timeout), std::invalid_argument);
}

// ---------- secret parsing and redaction -----------------------------------------------------------------------------

TEST(ProxySecretParse, PrefixesAndCaseAreRecognised) {
    const ProxySecret plain = parse_proxy_secret(kSecretHex);
    EXPECT_EQ(plain.kind, ProxySecretKind::Plain);
    EXPECT_EQ(to_hex(Bytes(plain.key.begin(), plain.key.end())), std::string(kSecretHex));

    const ProxySecret dd = parse_proxy_secret(std::string("dd") + kSecretHex);
    EXPECT_EQ(dd.kind, ProxySecretKind::PaddedIntermediate);
    EXPECT_EQ(to_hex(Bytes(dd.key.begin(), dd.key.end())), std::string(kSecretHex));

    std::string upper = kSecretHex;
    for (char& ch : upper)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    EXPECT_EQ(parse_proxy_secret(upper).key, plain.key);

    const std::string tls_hex = std::string("ee") + kSecretHex + "6578616d706c652e636f6d"; // ee + key + "example.com"
    const ProxySecret tls = parse_proxy_secret(tls_hex);
    EXPECT_EQ(tls.kind, ProxySecretKind::FakeTls);
    EXPECT_EQ(tls.fake_tls_domain, "example.com");
}

TEST(ProxySecretParse, MalformedInputIsRejectedWithoutEchoingIt) {
    const std::vector<std::string> bad = {
        std::string(kSecretHex).substr(2), // 30 hex digits: too short
        std::string(kSecretHex) + "00", // 34 hex digits: too long for plain
        std::string("zz") + std::string(kSecretHex).substr(2), // not hex
        std::string(""),
    };
    for (const std::string& input : bad) {
        std::string message;
        bool threw = false;
        try {
            (void)parse_proxy_secret(input);
        } catch (const std::invalid_argument& e) {
            threw = true;
            message = e.what();
        }
        EXPECT_TRUE(threw) << "accepted: " << input.size() << " chars";
        if (!input.empty())
            EXPECT_EQ(message.find(input), std::string::npos) << "error text echoes the input";
    }
}

// ---------- loopback MTProxy stub ------------------------------------------------------------------------------------

TEST(ProxyStub, IntermediateRoundTrip) {
    StubProxy stub(stub_key(), StubMode::Valid);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply = conn.receive_packet();
    }
    stub.join();
    const StubResult r = stub.result();
    EXPECT_TRUE(r.handshake_ok);
    EXPECT_EQ(framing_of_tag(r.tag), StubFraming::Intermediate);
    EXPECT_EQ(r.dc, 2);
    EXPECT_EQ(r.frames, 1u);
    EXPECT_EQ(r.first_payload, text("ABCDEFGH"));
    EXPECT_EQ(reply, text(kReplyText));
}

TEST(ProxyStub, PaddedIntermediateFollowsDdSecret) {
    StubProxy stub(stub_key(), StubMode::Valid);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), dd_secret(), 2, std::chrono::milliseconds(2000));
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply = conn.receive_packet();
    }
    stub.join();
    const StubResult r = stub.result();
    EXPECT_EQ(framing_of_tag(r.tag), StubFraming::Padded);
    EXPECT_EQ(r.frames, 1u);
    // The stub keeps the padding, so the payload is a prefix of what it got and the length word covers the padding.
    ASSERT_GE(r.first_payload.size(), 8u);
    EXPECT_LE(r.first_payload.size(), 23u);
    EXPECT_TRUE(std::equal(r.first_payload.begin(), r.first_payload.begin() + 8, text("ABCDEFGH").begin()));
    EXPECT_EQ(r.first_length_word, r.first_payload.size());
    EXPECT_EQ(reply, text(kReplyText));
}

TEST(ProxyStub, AbridgedRoundTripWithExplicitFraming) {
    StubProxy stub(stub_key(), StubMode::Valid);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000),
                                ObfuscatedFraming::Abridged);
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply = conn.receive_packet();
    }
    stub.join();
    const StubResult r = stub.result();
    EXPECT_EQ(framing_of_tag(r.tag), StubFraming::Abridged);
    EXPECT_EQ(r.frames, 1u);
    EXPECT_EQ(r.first_payload, text("ABCDEFGH"));
    EXPECT_EQ(reply, text(kReplyText));
}

TEST(ProxyStub, DcIdIsCarriedInTheHandshake) {
    for (int dc : { 2, -2, 10002 }) {
        SCOPED_TRACE("dc " + std::to_string(dc));
        StubProxy stub(stub_key(), StubMode::Valid);
        {
            Connection conn;
            conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), static_cast<std::int16_t>(dc),
                                    std::chrono::milliseconds(2000));
            conn.set_receive_timeout(5000);
            conn.send_packet(text("ABCDEFGH"));
            (void)conn.receive_packet();
        }
        stub.join();
        EXPECT_EQ(stub.result().dc, dc);
    }
}

TEST(ProxyStub, FragmentedReplyIsReassembled) {
    StubProxy stub(stub_key(), StubMode::FragmentedReply);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply = conn.receive_packet();
    }
    stub.join();
    EXPECT_EQ(reply, text(kReplyText));
}

TEST(ProxyStub, OneMebibytePacketIsSentCompletely) {
    Bytes big(1u << 20);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<std::uint8_t>((i * 31) & 0xff);
    StubProxy stub(stub_key(), StubMode::Valid);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
        conn.set_receive_timeout(10000);
        conn.send_packet(big);
        reply = conn.receive_packet();
    }
    stub.join();
    EXPECT_EQ(stub.result().frames, 1u);
    EXPECT_EQ(stub.result().first_payload, big);
    EXPECT_EQ(reply, text(kReplyText));
}

// A -404 transport error where a frame length is expected must fail, not be read as a frame.
TEST(ProxyStub, NegativeTransportErrorIsNotAFrame) {
    StubProxy stub(stub_key(), StubMode::GarbageReply);
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    conn.send_packet(text("ABCDEFGH"));
    bool threw = false;
    try {
        (void)conn.receive_packet();
    } catch (const ConnectionTimeout&) {
        ADD_FAILURE() << "client waited for a frame instead of rejecting the -404 reply";
        threw = true;
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(ProxyStub, SilentProxyIsAProxyErrorWithoutTheSecret) {
    StubProxy stub(stub_key(), StubMode::Silent);
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(300);
    conn.send_packet(text("ABCDEFGH"));
    bool proxy_error = false;
    std::string message;
    try {
        (void)conn.receive_packet();
    } catch (const ProxyError& e) {
        proxy_error = e.transient();
        message = e.what();
    } catch (...) { }
    EXPECT_TRUE(proxy_error);
    EXPECT_NE(message.find("no data"), std::string::npos);
    EXPECT_EQ(message.find(kSecretHex), std::string::npos);
}

TEST(ProxyStub, ProxyClosingAfterHandshakeIsAProxyError) {
    StubProxy stub(stub_key(), StubMode::CloseAfterHandshake);
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", stub.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    bool proxy_error = false;
    std::string message;
    try {
        (void)conn.receive_packet();
    } catch (const ProxyError& e) {
        proxy_error = e.transient();
        message = e.what();
    } catch (...) { }
    EXPECT_TRUE(proxy_error);
    EXPECT_NE(message.find("before sending any data"), std::string::npos);
}

// A wrong secret makes the stub decrypt the tag to noise, so it closes. The client must report that as a proxy
// failure and must not print the secret.
TEST(ProxyStub, WrongSecretIsAProxyErrorWithoutTheSecret) {
    StubProxy stub(stub_key(), StubMode::Valid);
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", stub.port(), wrong_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    bool proxy_error = false;
    std::string message;
    try {
        (void)conn.receive_packet();
    } catch (const ProxyError& e) {
        proxy_error = e.transient();
        message = e.what();
    } catch (...) { }
    EXPECT_TRUE(proxy_error);
    EXPECT_EQ(message.find(kSecretHex), std::string::npos);
}

// The same Connection must recover after a failed proxy attempt.
TEST(ProxyStub, ReconnectAfterFailureSucceeds) {
    StubProxy bad(stub_key(), StubMode::CloseAfterHandshake);
    StubProxy good(stub_key(), StubMode::Valid);
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", bad.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    EXPECT_THROW((void)conn.receive_packet(), ProxyError);

    conn.connect_obfuscated("127.0.0.1", good.port(), plain_secret(), 2, std::chrono::milliseconds(2000));
    conn.set_receive_timeout(5000);
    conn.send_packet(text("ABCDEFGH"));
    EXPECT_EQ(conn.receive_packet(), text(kReplyText));
}

// ---------- loopback SOCKS5 stub -------------------------------------------------------------------------------------

TEST(Socks5Stub, FragmentedMethodAndConnectRepliesAreAccepted) {
    Socks5Server server(Socks5Stub::FragmentedOk);
    const int fd = socks5_connect(socks5_options(server.port), "127.0.0.1", 443);
    EXPECT_GE(fd, 0);
    if (fd >= 0)
        ::close(fd);
}

TEST(Socks5Stub, WrongVersionInGreetingIsRejected) {
    Socks5Server server(Socks5Stub::WrongVersionInGreeting);
    EXPECT_THROW((void)socks5_connect(socks5_options(server.port), "127.0.0.1", 443), Socks5Error);
}

TEST(Socks5Stub, WrongVersionInConnectReplyIsRejected) {
    Socks5Server server(Socks5Stub::WrongVersionInReply);
    EXPECT_THROW((void)socks5_connect(socks5_options(server.port), "127.0.0.1", 443), Socks5Error);
}

TEST(Socks5Stub, UnknownAddressTypeInReplyIsRejected) {
    Socks5Server server(Socks5Stub::UnknownAddressTypeInReply);
    EXPECT_THROW((void)socks5_connect(socks5_options(server.port), "127.0.0.1", 443), Socks5Error);
}

TEST(Socks5Stub, RefusedConnectCarriesTheReplyCode) {
    Socks5Server server(Socks5Stub::Refused);
    int code = -1;
    try {
        (void)socks5_connect(socks5_options(server.port), "127.0.0.1", 443);
    } catch (const Socks5Error& e) {
        code = e.reply_code();
    }
    EXPECT_EQ(code, 5);
}

} // namespace

// ---------- Base64 secrets through the transport ---------------------------------------------------------------------

namespace {
const std::array<std::uint8_t, 16> kSynthKey
    = { 0xfb, 0xff, 0xbe, 0x3e, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb };

// Runs one exchange through the stub with a parsed secret and returns what the stub saw.
StubResult exchange_with_parsed_secret(const std::string& secret_text, Bytes& reply_out) {
    StubProxy stub(kSynthKey, StubMode::Valid);
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), parse_proxy_secret(secret_text), 2,
                                std::chrono::milliseconds(2000));
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply_out = conn.receive_packet();
    }
    stub.join();
    return stub.result();
}
} // namespace

// The padded Base64 secret must select padded intermediate on the wire, as its hex form does.
TEST(ProxySecretBase64Transport, PaddedBase64SecretSelectsPaddedFraming) {
    Bytes reply;
    const StubResult r = exchange_with_parsed_secret("3fv_vj4AESIzRFVmd4iZqrs=", reply);
    EXPECT_EQ(framing_of_tag(r.tag), StubFraming::Padded);
    EXPECT_EQ(r.frames, 1u);
    EXPECT_EQ(reply, text(kReplyText));
}

// A plain Base64 secret in standard alphabet must select plain intermediate.
TEST(ProxySecretBase64Transport, PlainStandardBase64SecretSelectsIntermediate) {
    Bytes reply;
    const StubResult r = exchange_with_parsed_secret("+/++PgARIjNEVWZ3iJmquw==", reply);
    EXPECT_EQ(framing_of_tag(r.tag), StubFraming::Intermediate);
    EXPECT_EQ(reply, text(kReplyText));
}

// Fake-TLS parses from Base64, but the transport still refuses it before any socket is opened (port 9 is never used).
TEST(ProxySecretBase64Transport, FakeTlsBase64SecretIsStillRefused) {
    Connection conn;
    const ProxySecret tls = parse_proxy_secret("7vv_vj4AESIzRFVmd4iZqrtleGFtcGxlLmNvbQ==");
    ASSERT_EQ(tls.kind, ProxySecretKind::FakeTls);
    EXPECT_THROW(conn.connect_obfuscated("127.0.0.1", 9, tls, 2, std::chrono::milliseconds(1000)),
                 std::invalid_argument);
}

// The padded transport passes trailing padding up to the caller unchanged. The plain packet decoder ignores it (see
// Handshake.PlainPacketIgnoresTransportPaddingUpToFifteenBytes in handshake_test.cpp).
TEST(ProxyStub, PaddedReplyKeepsItsPaddingForTheMtprotoLayer) {
    StubProxy stub(stub_key(), StubMode::PaddedReply);
    Bytes reply;
    {
        Connection conn;
        conn.connect_obfuscated("127.0.0.1", stub.port(), dd_secret(), 2, std::chrono::milliseconds(2000));
        conn.set_receive_timeout(5000);
        conn.send_packet(text("ABCDEFGH"));
        reply = conn.receive_packet();
    }
    stub.join();
    const Bytes expected = text(kReplyText);
    ASSERT_EQ(reply.size(), expected.size() + 5);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), reply.begin()));
}
