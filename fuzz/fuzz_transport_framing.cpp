// libFuzzer target: transport framing (abridged / intermediate / full) on attacker-controlled byte streams.
// Contract under test: only TransportError may escape; frames above the cap are rejected, never allocated.
#include <cstddef>
#include <cstdint>

#include "cppgram/mtproto/transport.h"

using namespace cppgram::mtproto;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const TransportKind kinds[] = {TransportKind::Abridged, TransportKind::Intermediate, TransportKind::Full};
    Transport t(kinds[data[0] % 3]);
    const std::size_t chunk = 1 + (data[0] >> 2) % 64;  // feed in varying chunk sizes
    try {
        for (std::size_t off = 1; off < size; off += chunk) {
            const std::size_t n = (off + chunk <= size) ? chunk : size - off;
            t.feed(Bytes(data + off, data + off + n));
            while (t.next().has_value()) {
            }
        }
    } catch (const TransportError&) {
    }
    return 0;
}
