#pragma once

#include <cstdint>
#include <string>

#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

// Parameters from account.getPassword (passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow)
// plus the server value B (srp_B).
struct SrpParams {
    Bytes salt1;
    Bytes salt2;
    std::int32_t g = 0;
    Bytes p;
    Bytes server_b;
};

struct SrpCheck {
    Bytes A;   // client public value, 256 bytes
    Bytes M1;  // proof sent to auth.checkPassword
    Bytes K;   // session key H(S)
    Bytes S;   // shared secret, 256 bytes
};

// x = PH2(password, salt1, salt2): the password-derived private exponent.
Bytes srp_password_x(const Bytes& password, const Bytes& salt1, const Bytes& salt2);

// v = g^x mod p, padded to 256 bytes. This is the value stored by the server as the new password hash.
Bytes srp_verifier(const Bytes& password, const Bytes& salt1, const Bytes& salt2, std::int32_t g, const Bytes& p);

// Computes the client side of Telegram's 2FA SRP exchange. random_a must be 256 random bytes.
SrpCheck compute_srp_check(const Bytes& password, const SrpParams& params, const Bytes& random_a);

}  // namespace cppgram::mtproto
