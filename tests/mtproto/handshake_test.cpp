#include <gtest/gtest.h>

#include <ctime>
#include <deque>
#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"

using namespace cppgram::mtproto;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;

namespace {

Bytes be_bytes(const BIGNUM* n, std::size_t size) {
    Bytes out(size, 0);
    BN_bn2binpad(n, out.data(), static_cast<int>(size));
    return out;
}

// In-process Telegram-like server: answers each request synchronously with the protocol replies.
class FakeServer : public PlainChannel {
public:
    FakeServer() {
        rsa_ = RSA_new();
        BIGNUM* e = BN_new();
        BN_set_word(e, 65537);
        RSA_generate_key_ex(rsa_, 2048, e, nullptr);
        BN_free(e);
        const BIGNUM *n = nullptr, *ed = nullptr, *d = nullptr;
        RSA_get0_key(rsa_, &n, &ed, &d);
        modulus_ = be_bytes(n, 256);
        exponent_ = Bytes{0x01, 0x00, 0x01};
        private_exp_ = d ? BN_dup(d) : nullptr;
        modulus_bn_ = BN_dup(n);

        // 2048-bit prime for the DH group (primality only; the client does not check safe-prime structure).
        dh_prime_ = BN_new();
        BN_generate_prime_ex(dh_prime_, 2048, 0, nullptr, nullptr, nullptr);
        dh_prime_bytes_ = be_bytes(dh_prime_, 256);
    }
    ~FakeServer() override {
        RSA_free(rsa_);
        BN_free(private_exp_);
        BN_free(modulus_bn_);
        BN_free(dh_prime_);
    }

    RsaPublicKey public_key() const { return RsaPublicKey{modulus_, exponent_}; }

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
        res.server_public_key_fingerprints = {static_cast<std::int64_t>(rsa_fingerprint(modulus_, exponent_))};
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

    RSA* rsa_ = nullptr;
    Bytes modulus_, exponent_;
    BIGNUM* private_exp_ = nullptr;
    BIGNUM* modulus_bn_ = nullptr;
    BIGNUM* dh_prime_ = nullptr;
    Bytes dh_prime_bytes_;
    Bytes nonce_, server_nonce_, new_nonce_, g_, a_;
    std::deque<Bytes> replies_;
};

}  // namespace

TEST(Handshake, CreatesMatchingAuthKeyAgainstFakeServer) {
    FakeServer server;
    HandshakeOptions options;
    options.keys = {server.public_key()};
    const AuthKeyResult result = create_auth_key(server, options);
    EXPECT_EQ(result.auth_key.size(), 256u);
    EXPECT_EQ(result.auth_key, server.server_auth_key);
    EXPECT_EQ(result.server_salt, server.server_salt);
}

TEST(Handshake, FailsWhenNoKeyMatchesFingerprint) {
    FakeServer server;
    HandshakeOptions options;
    options.keys = {RsaPublicKey{Bytes(256, 0x11), Bytes{0x01, 0x00, 0x01}}};
    EXPECT_THROW(create_auth_key(server, options), std::exception);
}

TEST(Handshake, PlainPacketRoundTrip) {
    const Bytes body = Bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const Bytes packet = wrap_plain_message(body, 0x1234500000000004ull);
    EXPECT_EQ(unwrap_plain_message(packet), body);
    EXPECT_EQ(make_message_id() % 4, 0u);
}

TEST(Handshake, TelegramKeysParse) {
    const auto& keys = telegram_public_keys();
    ASSERT_EQ(keys.size(), 5u);
    EXPECT_EQ(keys[0].modulus.size(), 256u);
    EXPECT_EQ(keys[0].exponent, (Bytes{0x01, 0x00, 0x01}));
}
