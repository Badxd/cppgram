// libFuzzer target: unauthenticated (auth_key_id = 0) packets as received during the handshake.
// Entry point under test: cppgram::mtproto::unwrap_plain_message (declared public in auth.h).
// Contract: only AuthError may escape. Sanitizer reports and any other exception type are defects.
#include <cstddef>
#include <cstdint>

#include "cppgram/mtproto/auth.h"

using namespace cppgram::mtproto;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const Bytes packet(data, data + size);
    try {
        const Bytes body = unwrap_plain_message(packet);
        // A returned body must never be larger than the packet it came from.
        if (body.size() > packet.size()) __builtin_trap();
    } catch (const AuthError&) {
    }
    return 0;
}
