// libFuzzer target: encrypted packets (auth_key_id | msg_key | ciphertext) under a fixed auth key, both directions.
// Contract: only CryptoError or TlError may escape. A bad msg_key or auth_key_id must surface as CryptoError.
#include <cstddef>
#include <cstdint>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/tl.h"

using namespace cppgram::mtproto;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    static const Bytes key(256, 0x5a);
    const bool outgoing = (data[0] & 1) != 0;
    const Bytes packet(data + 1, data + size);
    try {
        const Bytes plain = decrypt_packet(key, packet, outgoing);
        const InnerMessage inner = parse_inner(plain);
        if (inner.body.size() > plain.size()) __builtin_trap();
    } catch (const CryptoError&) {
    } catch (const TlError&) {
    }
    return 0;
}
