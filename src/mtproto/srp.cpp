#include "cppgram/mtproto/srp.h"

#include <openssl/bn.h>
#include <openssl/evp.h>

#include <memory>
#include <stdexcept>

#include "cppgram/mtproto/crypto.h"

namespace cppgram::mtproto {

namespace {

constexpr std::size_t kModulusBytes = 256;  // 2048 bits
constexpr int kPbkdf2Rounds = 100000;

using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using CtxPtr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;

BnPtr bn_new() { return BnPtr(BN_new(), BN_free); }

BnPtr bn_from(const Bytes& data) {
    BnPtr r = bn_new();
    if (!r || BN_bin2bn(data.data(), static_cast<int>(data.size()), r.get()) == nullptr) {
        throw std::runtime_error("srp: BN_bin2bn failed");
    }
    return r;
}

Bytes bn_pad(const BIGNUM* v, std::size_t size) {
    Bytes out(size);
    if (BN_bn2binpad(v, out.data(), static_cast<int>(size)) < 0) {
        throw std::runtime_error("srp: value does not fit in " + std::to_string(size) + " bytes");
    }
    return out;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// H(data) := sha256(data)
Bytes H(const Bytes& data) { return sha256(data); }

// SH(data, salt) := H(salt | data | salt)
Bytes SH(const Bytes& data, const Bytes& salt) { return H(concat({salt, data, salt})); }

Bytes pbkdf2_sha512(const Bytes& password, const Bytes& salt, int rounds, std::size_t out_len) {
    Bytes out(out_len);
    if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password.data()), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()), rounds, EVP_sha512(),
                          static_cast<int>(out_len), out.data()) != 1) {
        throw std::runtime_error("srp: PBKDF2 failed");
    }
    return out;
}

Bytes g_bytes(std::int32_t g) {
    BnPtr v = bn_new();
    BN_set_word(v.get(), static_cast<BN_ULONG>(g));
    return bn_pad(v.get(), kModulusBytes);
}

}  // namespace

Bytes srp_password_x(const Bytes& password, const Bytes& salt1, const Bytes& salt2) {
    // PH1 = SH(SH(password, salt1), salt2)
    const Bytes ph1 = SH(SH(password, salt1), salt2);
    // PH2 = SH(pbkdf2(sha512, PH1, salt1, 100000), salt2)
    return SH(pbkdf2_sha512(ph1, salt1, kPbkdf2Rounds, 64), salt2);
}

SrpCheck compute_srp_check(const Bytes& password, const SrpParams& params, const Bytes& random_a) {
    if (random_a.size() != kModulusBytes) throw std::invalid_argument("srp: random_a must be 256 bytes");
    if (params.p.size() != kModulusBytes) throw std::runtime_error("srp: modulus p is not 2048 bits");
    if (params.salt1.empty() || params.salt2.empty()) throw std::runtime_error("srp: missing salts");

    CtxPtr ctx(BN_CTX_new(), BN_CTX_free);
    if (!ctx) throw std::runtime_error("srp: BN_CTX_new failed");

    BnPtr p = bn_from(params.p);
    BnPtr g = bn_new();
    BN_set_word(g.get(), static_cast<BN_ULONG>(params.g));
    const Bytes g_pad = g_bytes(params.g);
    const Bytes p_pad = bn_pad(p.get(), kModulusBytes);

    // x and v = g^x mod p
    BnPtr x = bn_from(srp_password_x(password, params.salt1, params.salt2));
    BnPtr v = bn_new();
    if (BN_mod_exp(v.get(), g.get(), x.get(), p.get(), ctx.get()) != 1) throw std::runtime_error("srp: v failed");

    // k = H(p | g), k_v = k * v mod p
    BnPtr k = bn_from(H(concat({p_pad, g_pad})));
    BnPtr kv = bn_new();
    if (BN_mod_mul(kv.get(), k.get(), v.get(), p.get(), ctx.get()) != 1) throw std::runtime_error("srp: k_v failed");

    // A = g^a mod p with a random 2048-bit a
    BnPtr a = bn_from(random_a);
    BnPtr A = bn_new();
    if (BN_mod_exp(A.get(), g.get(), a.get(), p.get(), ctx.get()) != 1) throw std::runtime_error("srp: A failed");
    if (BN_is_zero(A.get())) throw std::runtime_error("srp: A is zero mod p, retry with new random_a");

    // B from the server, u = H(A | B)
    BnPtr B = bn_from(params.server_b);
    const Bytes A_pad = bn_pad(A.get(), kModulusBytes);
    const Bytes B_pad = bn_pad(B.get(), kModulusBytes);
    BnPtr u = bn_from(H(concat({A_pad, B_pad})));
    if (BN_is_zero(u.get())) throw std::runtime_error("srp: u is zero");

    // t = (B - k_v) mod p, exponent = a + u * x, S = t^exponent mod p
    BnPtr t = bn_new();
    if (BN_mod_sub(t.get(), B.get(), kv.get(), p.get(), ctx.get()) != 1) throw std::runtime_error("srp: t failed");
    BnPtr ux = bn_new();
    BnPtr exponent = bn_new();
    if (BN_mul(ux.get(), u.get(), x.get(), ctx.get()) != 1 || BN_add(exponent.get(), a.get(), ux.get()) != 1) {
        throw std::runtime_error("srp: exponent failed");
    }
    BnPtr S = bn_new();
    if (BN_mod_exp(S.get(), t.get(), exponent.get(), p.get(), ctx.get()) != 1) throw std::runtime_error("srp: S failed");

    SrpCheck out;
    out.A = A_pad;
    out.S = bn_pad(S.get(), kModulusBytes);
    out.K = H(out.S);

    // M1 = H( H(p) xor H(g) | H(salt1) | H(salt2) | A | B | K )
    Bytes hp = H(p_pad);
    const Bytes hg = H(g_pad);
    for (std::size_t i = 0; i < hp.size(); ++i) hp[i] ^= hg[i];
    out.M1 = H(concat({hp, H(params.salt1), H(params.salt2), A_pad, B_pad, out.K}));
    return out;
}

}  // namespace cppgram::mtproto

namespace cppgram::mtproto {

Bytes srp_verifier(const Bytes& password, const Bytes& salt1, const Bytes& salt2, std::int32_t g, const Bytes& p) {
    if (p.size() != kModulusBytes) throw std::runtime_error("srp: modulus p is not 2048 bits");
    CtxPtr ctx(BN_CTX_new(), BN_CTX_free);
    if (!ctx) throw std::runtime_error("srp: BN_CTX_new failed");
    BnPtr x = bn_from(srp_password_x(password, salt1, salt2));
    BnPtr modulus = bn_from(p);
    BnPtr base = bn_new();
    BN_set_word(base.get(), static_cast<BN_ULONG>(g));
    BnPtr v = bn_new();
    if (BN_mod_exp(v.get(), base.get(), x.get(), modulus.get(), ctx.get()) != 1) {
        throw std::runtime_error("srp: verifier failed");
    }
    return bn_pad(v.get(), kModulusBytes);
}

}  // namespace cppgram::mtproto
