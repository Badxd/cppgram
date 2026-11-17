#include <gtest/gtest.h>
#include <openssl/bn.h>

#include <memory>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/srp.h"

namespace {

using cppgram::mtproto::Bytes;
using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;

BnPtr bn(const Bytes& b) {
    BnPtr r(BN_new(), BN_free);
    BN_bin2bn(b.data(), static_cast<int>(b.size()), r.get());
    return r;
}

Bytes pad256(const BIGNUM* v) {
    Bytes out(256);
    BN_bn2binpad(v, out.data(), 256);
    return out;
}

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

}  // namespace

// Checks the client computation against the server formula from core.telegram.org/api/srp:
// server computes s_b = (g_a * v^u)^b mod p, client computes s_a = (g_b - k*v)^(a + u*x) mod p.
// Both must match. The modulus is 2048 bits so the sizes match production parameters.
TEST(Srp, ClientAndServerAgreeOnSharedSecret) {
    const Bytes password = {'h', 'u', 'n', 't', 'e', 'r', '2'};
    cppgram::mtproto::SrpParams params;
    params.salt1 = Bytes(16, 0x11);
    params.salt2 = Bytes(16, 0x22);
    params.g = 3;
    params.p = Bytes(256, 0xFF);
    params.p[0] = 0x7F;  // p = 2^2047 - 1 (full 2048-bit size; the identity below holds for any modulus)

    const Bytes random_a = cppgram::mtproto::random_bytes(256);
    const Bytes server_b_secret = cppgram::mtproto::random_bytes(32);

    BN_CTX* raw_ctx = BN_CTX_new();
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx(raw_ctx, BN_CTX_free);
    BnPtr p = bn(params.p);
    BnPtr g(BN_new(), BN_free);
    BN_set_word(g.get(), 3);
    const Bytes p_pad = pad256(p.get());
    const Bytes g_pad = pad256(g.get());

    // Server: v = g^x, k_v = k*v, g_b = k_v + g^b mod p
    BnPtr x = bn(cppgram::mtproto::srp_password_x(password, params.salt1, params.salt2));
    BnPtr v(BN_new(), BN_free);
    BN_mod_exp(v.get(), g.get(), x.get(), p.get(), ctx.get());
    BnPtr k = bn(cppgram::mtproto::sha256(cat({p_pad, g_pad})));
    BnPtr kv(BN_new(), BN_free);
    BN_mod_mul(kv.get(), k.get(), v.get(), p.get(), ctx.get());
    BnPtr b = bn(server_b_secret);
    BnPtr gb(BN_new(), BN_free);
    BN_mod_exp(gb.get(), g.get(), b.get(), p.get(), ctx.get());
    BnPtr server_B(BN_new(), BN_free);
    BN_mod_add(server_B.get(), kv.get(), gb.get(), p.get(), ctx.get());
    params.server_b = pad256(server_B.get());

    const auto check = cppgram::mtproto::compute_srp_check(password, params, random_a);

    // Server side: u = H(A | B), s_b = (A * v^u)^b mod p
    BnPtr A = bn(check.A);
    BnPtr u = bn(cppgram::mtproto::sha256(cat({check.A, params.server_b})));
    BnPtr vu(BN_new(), BN_free);
    BN_mod_exp(vu.get(), v.get(), u.get(), p.get(), ctx.get());
    BnPtr base(BN_new(), BN_free);
    BN_mod_mul(base.get(), A.get(), vu.get(), p.get(), ctx.get());
    BnPtr sb(BN_new(), BN_free);
    BN_mod_exp(sb.get(), base.get(), b.get(), p.get(), ctx.get());

    EXPECT_EQ(check.S, pad256(sb.get()));
    EXPECT_EQ(check.A.size(), 256u);
    EXPECT_EQ(check.M1.size(), 32u);
    EXPECT_EQ(check.K.size(), 32u);
}

TEST(Srp, DifferentPasswordsGiveDifferentX) {
    const Bytes salt1(16, 1), salt2(16, 2);
    const auto x1 = cppgram::mtproto::srp_password_x({'a'}, salt1, salt2);
    const auto x2 = cppgram::mtproto::srp_password_x({'b'}, salt1, salt2);
    EXPECT_EQ(x1.size(), 32u);
    EXPECT_NE(x1, x2);
}

TEST(Srp, VerifierIsGToTheX) {
    const Bytes password = {'n', 'e', 'w'};
    const Bytes salt1(16, 0x33), salt2(16, 0x44);
    Bytes p(256, 0xFF);
    p[0] = 0x7F;
    const std::int32_t g = 3;

    const Bytes v = cppgram::mtproto::srp_verifier(password, salt1, salt2, g, p);
    ASSERT_EQ(v.size(), 256u);

    BN_CTX* raw = BN_CTX_new();
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx(raw, BN_CTX_free);
    BnPtr x = bn(cppgram::mtproto::srp_password_x(password, salt1, salt2));
    BnPtr pn = bn(p);
    BnPtr gn(BN_new(), BN_free);
    BN_set_word(gn.get(), 3);
    BnPtr expected(BN_new(), BN_free);
    BN_mod_exp(expected.get(), gn.get(), x.get(), pn.get(), ctx.get());
    EXPECT_EQ(v, pad256(expected.get()));
}
