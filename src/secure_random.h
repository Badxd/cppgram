#pragma once

#include <cstdint>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/session.h"

namespace cppgram::detail {

// Cryptographically secure 64-bit value (OpenSSL RAND_bytes). Used for random_id and similar nonces.
inline std::int64_t secure_random_i64() {
    const mtproto::Bytes rnd = mtproto::random_bytes(8);
    return static_cast<std::int64_t>(mtproto::le_u64_from(rnd));
}

}  // namespace cppgram::detail
