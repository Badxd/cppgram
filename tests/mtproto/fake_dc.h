// Test-only fake Telegram DC. Header-only so both the handshake tests and the client tests can use it.
// FakeServer answers the plain (unencrypted) key-exchange requests in-process. FakeDc serves them over a
// real loopback TCP socket with the intermediate transport and then answers encrypted requests, so
// cppgram::Client can be driven through connect, reconnect and FLOOD_WAIT paths. Nothing here talks to the
// network beyond 127.0.0.1, and nothing here implements the real Telegram server.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <deque>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"

namespace cppgram::testing {

using namespace cppgram::mtproto;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;


inline Bytes be_bytes(const BIGNUM* n, std::size_t size) {
    Bytes out(size, 0);
    BN_bn2binpad(n, out.data(), static_cast<int>(size));
    return out;
}

// In-process Telegram-like handshake server: answers each plain request synchronously.
inline constexpr const char* kSafePrimeHex =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF";

class FakeServer : public PlainChannel {
public:
    FakeServer() {
        // OpenSSL 3 EVP API (RSA_new/RSA_generate_key_ex are deprecated).
        EVP_PKEY* key = nullptr;
        EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
        unsigned int bits = 2048;
        OSSL_PARAM params[] = {OSSL_PARAM_construct_uint(OSSL_PKEY_PARAM_RSA_BITS, &bits),
                               OSSL_PARAM_construct_end()};
        if (kctx == nullptr || EVP_PKEY_keygen_init(kctx) <= 0 || EVP_PKEY_CTX_set_params(kctx, params) <= 0 ||
            EVP_PKEY_keygen(kctx, &key) <= 0) {
            EVP_PKEY_CTX_free(kctx);
            throw std::runtime_error("test RSA key generation failed");
        }
        EVP_PKEY_CTX_free(kctx);
        BIGNUM* n = nullptr;
        BIGNUM* d = nullptr;
        EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_N, &n);
        EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_D, &d);
        EVP_PKEY_free(key);
        modulus_ = be_bytes(n, 256);
        exponent_ = Bytes{0x01, 0x00, 0x01};
        private_exp_ = d;  // owned by this object, freed in the destructor
        modulus_bn_ = n;

        // RFC 3526 group 14: a 2048-bit safe prime with generator 2 (verified safe prime).
        dh_prime_ = BN_new();
        BN_hex2bn(&dh_prime_, kSafePrimeHex);
        dh_prime_bytes_ = be_bytes(dh_prime_, 256);
    }
    ~FakeServer() override {
        BN_free(private_exp_);
        BN_free(modulus_bn_);
        BN_free(dh_prime_);
    }

    RsaPublicKey public_key() { return RsaPublicKey{modulus_, exponent_}; }

    void send_plain(const Bytes& packet) override {
        const Bytes body = unwrap_plain_message(packet);
        TlReader r(body);
        const std::uint32_t id = r.read_uint32();
        if (id == fn::req_pq_multi::kId) {
            handle_req_pq(r);
        } else if (id == fn::req_DH_params::kId) {
            handle_req_dh(r);
        } else if (id == fn::set_client_DH_params::kId) {
            handle_set_client(r);
        } else {
            throw std::runtime_error("fake server: unexpected request");
        }
    }

    Bytes receive_plain() override {
        if (replies_.empty()) {
            throw std::runtime_error("fake server: no reply queued");
        }
        Bytes out = std::move(replies_.front());
        replies_.pop_front();
        return wrap_plain_message(out, make_message_id());
    }

    Bytes server_auth_key;
    std::uint64_t server_salt = 0;

private:
    void reply(TlWriter& w) { replies_.push_back(w.take()); }

    void handle_req_pq(TlReader& r) {
        nonce_ = r.read_raw(16);
        server_nonce_ = random_bytes(16);
        ty::resPQ res;
        res.nonce = nonce_;
        res.server_nonce = server_nonce_;
        // 1724114033281923457 = 1229739323 * 1402015859
        res.pq = Bytes{0x17, 0xED, 0x48, 0x94, 0x1A, 0x08, 0xF9, 0x81};
        // Built with push_back: GCC 12 reports a false -Wstringop-overread for vector assignment from an initializer list.
        res.server_public_key_fingerprints.clear();
        res.server_public_key_fingerprints.push_back(static_cast<std::int64_t>(rsa_fingerprint(modulus_, exponent_)));
        TlWriter w;
        res.serialize(w);
        reply(w);
    }

    Bytes rsa_decrypt(const Bytes& c) {
        BN_CTX* ctx = BN_CTX_new();
        BIGNUM* cb = BN_bin2bn(c.data(), static_cast<int>(c.size()), nullptr);
        BIGNUM* m = BN_new();
        BN_mod_exp(m, cb, private_exp_, modulus_bn_, ctx);
        Bytes out(255, 0);
        BN_bn2binpad(m, out.data(), 255);
        BN_free(cb);
        BN_free(m);
        BN_CTX_free(ctx);
        return out;
    }

    void handle_req_dh(TlReader& r) {
        const Bytes nonce = r.read_raw(16);
        const Bytes server_nonce = r.read_raw(16);
        r.read_bytes();  // p
        r.read_bytes();  // q
        r.read_int64();  // fingerprint
        const Bytes encrypted = r.read_bytes();
        if (nonce != nonce_ || server_nonce != server_nonce_) {
            throw std::runtime_error("fake server: nonce mismatch");
        }
        const Bytes plain = rsa_decrypt(encrypted);
        TlReader inner_r(Bytes(plain.begin() + 20, plain.end()));
        EXPECT_EQ(inner_r.read_uint32(), ty::p_q_inner_data_dc::kId);
        ty::p_q_inner_data_dc inner = ty::p_q_inner_data_dc::read_body(inner_r);
        new_nonce_ = inner.new_nonce;

        const TmpAes aes = tmp_aes();
        const Bytes a = random_bytes(256);
        g_ = Bytes{0x02};
        a_ = a;
        const Bytes g_a = dh_power(g_, a, dh_prime_bytes_);

        ty::server_DH_inner_data inner_out;
        inner_out.nonce = nonce_;
        inner_out.server_nonce = server_nonce_;
        inner_out.g = 2;
        inner_out.dh_prime = dh_prime_bytes_;
        inner_out.g_a = g_a;
        inner_out.server_time = static_cast<std::int32_t>(std::time(nullptr));
        TlWriter iw;
        inner_out.serialize(iw);
        Bytes data = concat(sha1(iw.bytes()), iw.bytes());
        data = concat(data, random_bytes((16 - data.size() % 16) % 16));

        ty::server_DH_params_ok ok;
        ok.nonce = nonce_;
        ok.server_nonce = server_nonce_;
        ok.encrypted_answer = aes_ige_encrypt(data, aes.key, aes.iv);
        TlWriter w;
        ok.serialize(w);
        reply(w);
    }

    void handle_set_client(TlReader& r) {
        r.read_raw(16);
        r.read_raw(16);
        const Bytes encrypted = r.read_bytes();
        const TmpAes aes = tmp_aes();
        const Bytes plain = aes_ige_decrypt(encrypted, aes.key, aes.iv);
        TlReader inner_r(Bytes(plain.begin() + 20, plain.end()));
        EXPECT_EQ(inner_r.read_uint32(), ty::client_DH_inner_data::kId);
        ty::client_DH_inner_data inner = ty::client_DH_inner_data::read_body(inner_r);

        server_auth_key = dh_power(inner.g_b, a_, dh_prime_bytes_);
        const Bytes aux = slice(sha1(server_auth_key), 0, 8);
        ty::dh_gen_ok ok;
        ok.nonce = nonce_;
        ok.server_nonce = server_nonce_;
        ok.new_nonce_hash1 = slice(sha1(concat(concat(new_nonce_, Bytes{0x01}), aux)), 4, 16);
        TlWriter w;
        ok.serialize(w);
        reply(w);

        server_salt = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            server_salt |= static_cast<std::uint64_t>(new_nonce_[i] ^ server_nonce_[i]) << (8 * i);
        }
    }

    struct TmpAes {
        Bytes key;
        Bytes iv;
    };

    TmpAes tmp_aes() const {
        const Bytes s1 = sha1(concat(new_nonce_, server_nonce_));
        const Bytes s2 = sha1(concat(server_nonce_, new_nonce_));
        const Bytes s3 = sha1(concat(new_nonce_, new_nonce_));
        TmpAes out;
        out.key = concat(s1, slice(s2, 0, 12));
        out.iv = concat(concat(slice(s2, 12, 8), s3), slice(new_nonce_, 0, 4));
        return out;
    }

    Bytes modulus_, exponent_;
    BIGNUM* private_exp_ = nullptr;
    BIGNUM* modulus_bn_ = nullptr;
    BIGNUM* dh_prime_ = nullptr;
    Bytes dh_prime_bytes_;
    Bytes nonce_, server_nonce_, new_nonce_, g_, a_;
    std::deque<Bytes> replies_;
};


// Loopback TCP fake DC. Serves one or more client connections and keeps one auth key for all of them, so a
// reconnecting client restores its session without a new handshake.
class FakeDc {
public:
    FakeDc() {
        const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        listen_fd_ = listen_fd;
        int one = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_fd, 8) != 0) {
            throw std::runtime_error("fake DC: cannot listen on loopback");
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        accept_thread_ = std::thread([this] { accept_loop(); });
    }

    ~FakeDc() {
        stopping_ = true;
        close_listener();
        drop_connections();
        if (accept_thread_.joinable()) accept_thread_.join();
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            workers.swap(workers_);
        }
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }

    FakeDc(const FakeDc&) = delete;
    FakeDc& operator=(const FakeDc&) = delete;

    std::uint16_t port() const noexcept { return port_; }

    // The RSA key the fake DC signs its handshake with. Pass it through ClientOptions::server_keys.
    RsaPublicKey public_key() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!handshake_) handshake_ = std::make_unique<FakeServer>();
        return handshake_->public_key();
    }

    // Shuts down every open client connection. The client sees the peer close the connection.
    void drop_connections() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : client_fds_) ::shutdown(fd, SHUT_RDWR);
    }

    // While refusing, new connections are closed right after accept, so the client's reconnect attempts fail.
    void set_refusing(bool refusing) { refusing_ = refusing; }

    // The next `count` help.getNearestDc requests are answered with rpc_error(420, message).
    void set_flood(int count, std::string message) {
        std::lock_guard<std::mutex> lock(mutex_);
        flood_remaining_ = count;
        flood_message_ = std::move(message);
    }

    int connections_accepted() const noexcept { return accepted_; }
    int flood_answers() const noexcept { return flood_answers_; }
    int requests_served() const noexcept { return served_; }

private:
    void close_listener() {
        const int fd = listen_fd_.exchange(-1);
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
    }

    static bool read_exact(int fd, std::uint8_t* out, std::size_t n) {
        std::size_t got = 0;
        while (got < n) {
            const ssize_t r = ::recv(fd, out + got, n - got, 0);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return false;
            got += static_cast<std::size_t>(r);
        }
        return true;
    }

    static bool write_all(int fd, const Bytes& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    static bool write_frame(int fd, const Bytes& body) {
        Bytes frame;
        const auto len = static_cast<std::uint32_t>(body.size());
        for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
        frame.insert(frame.end(), body.begin(), body.end());
        return write_all(fd, frame);
    }

    void accept_loop() {
        while (!stopping_) {
            const int listener = listen_fd_.load();
            if (listener < 0) break;
            const int fd = ::accept(listener, nullptr, nullptr);
            if (fd < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (stopping_) {
                ::close(fd);
                break;
            }
            ++accepted_;
            if (refusing_) {
                ::close(fd);
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            client_fds_.push_back(fd);
            workers_.emplace_back([this, fd] { serve(fd); });
        }
    }

    void forget(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = client_fds_.begin(); it != client_fds_.end(); ++it) {
            if (*it == fd) {
                client_fds_.erase(it);
                break;
            }
        }
        ::close(fd);
    }

    void serve(int fd) {
        std::uint8_t tag[4];
        if (!read_exact(fd, tag, 4)) {
            forget(fd);
            return;
        }
        for (;;) {
            std::uint8_t lenb[4];
            if (!read_exact(fd, lenb, 4)) break;
            const std::uint32_t len = lenb[0] | (lenb[1] << 8) | (lenb[2] << 16) | (static_cast<std::uint32_t>(lenb[3]) << 24);
            if (len == 0 || len > (1u << 20)) break;
            Bytes body(len);
            if (!read_exact(fd, body.data(), len)) break;
            Bytes reply;
            try {
                if (!handle(body, reply)) break;
            } catch (const std::exception&) {
                break;
            }
            if (!reply.empty() && !write_frame(fd, reply)) break;
        }
        forget(fd);
    }

    // Returns false to close the connection. `reply` is the packet to send back (may be empty).
    bool handle(const Bytes& body, Bytes& reply) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool plain = body.size() >= 8 && std::all_of(body.begin(), body.begin() + 8, [](std::uint8_t b) { return b == 0; });
        if (plain) {
            if (!handshake_) handshake_ = std::make_unique<FakeServer>();
            handshake_->send_plain(body);
            reply = handshake_->receive_plain();
            return true;
        }
        if (!handshake_ || handshake_->server_auth_key.empty()) return false;
        const Bytes& key = handshake_->server_auth_key;
        const InnerMessage inner = parse_inner(decrypt_packet(key, body, true));
        ++served_;
        if ((inner.seq_no & 1u) == 0) return true;  // acknowledgements and service messages get no reply
        if (inner.body.size() < 4) return false;

        TlReader r(inner.body);
        const std::uint32_t id = r.read_uint32();
        Bytes result;
        if (id == fn::ping_delay_disconnect::kId) {
            return true;  // the client sends this without waiting for an answer
        }
        if (id == fn::ping::kId) {
            ty::pong pong;
            pong.msg_id = static_cast<std::int64_t>(inner.message_id);
            pong.ping_id = r.read_int64();
            TlWriter w;
            pong.serialize(w);
            result = w.take();
        } else if (id == fn::invokeWithLayer<fn::initConnection<fn::help_getConfig>>::kId) {
            TlWriter w;
            ty::config cfg;
            cfg.serialize(w);
            result = w.take();
        } else if (id == fn::updates_getState::kId) {
            ty::updates_state st;
            st.pts = 1;
            st.qts = 0;
            st.date = static_cast<std::int32_t>(std::time(nullptr));
            st.seq = 1;
            TlWriter w;
            st.serialize(w);
            result = w.take();
        } else if (id == fn::updates_getDifference::kId) {
            ty::updates_differenceEmpty empty;  // nothing missed while disconnected
            empty.date = static_cast<std::int32_t>(std::time(nullptr));
            empty.seq = 1;
            TlWriter w;
            empty.serialize(w);
            result = w.take();
        } else if (id == fn::help_getNearestDc::kId) {
            if (flood_remaining_ > 0) {
                --flood_remaining_;
                ++flood_answers_;
                ty::rpc_error err;
                err.error_code = 420;
                err.error_message = flood_message_;
                TlWriter w;
                err.serialize(w);
                result = w.take();
            } else {
                ty::nearestDc near;
                near.country = "US";
                near.this_dc = 2;
                near.nearest_dc = 2;
                TlWriter w;
                near.serialize(w);
                result = w.take();
            }
        } else {
            ty::rpc_error err;
            err.error_code = 400;
            err.error_message = "FAKE_DC_UNSUPPORTED";
            TlWriter w;
            err.serialize(w);
            result = w.take();
        }

        TlWriter rw;
        rw.write_uint32(ty::rpc_result::kId);
        rw.write_uint64(inner.message_id);
        rw.write_raw(result);
        InnerMessage out;
        out.salt = 1;
        out.session_id = inner.session_id;
        out.seq_no = 1;
        out.message_id = make_message_id() + 4;
        out.body = rw.take();
        reply = encrypt_packet(key, serialize_inner(out), false);
        return true;
    }

    std::atomic<int> listen_fd_{-1};
    std::uint16_t port_ = 0;
    std::thread accept_thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> refusing_{false};
    std::atomic<int> accepted_{0};
    std::atomic<int> served_{0};
    std::atomic<int> flood_answers_{0};
    std::mutex mutex_;
    std::vector<int> client_fds_;
    std::vector<std::thread> workers_;
    std::unique_ptr<FakeServer> handshake_;
    int flood_remaining_ = 0;
    std::string flood_message_;
};

}  // namespace cppgram::testing
