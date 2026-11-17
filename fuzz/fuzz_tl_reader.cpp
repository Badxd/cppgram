// libFuzzer target: drives the TL primitives over arbitrary bytes.
// Any TlError / std::runtime_error is an expected rejection; crashes and sanitizer reports are bugs.
#include <cstddef>
#include <cstdint>
#include <exception>

#include "cppgram/mtproto/tl.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using cppgram::mtproto::Bytes;
    using cppgram::mtproto::TlReader;
    Bytes input(data, data + size);
    TlReader r(input);
    try {
        // Consume the input with a mix of primitives chosen by the first byte, so one input drives many paths.
        std::size_t step = 0;
        while (r.remaining() > 0) {
            const std::uint8_t op = input[step % input.size()] % 7;
            ++step;
            switch (op) {
                case 0: r.read_uint32(); break;
                case 1: r.read_uint64(); break;
                case 2: r.read_bytes(); break;
                case 3: r.read_string(); break;
                case 4: r.read_bool(); break;
                case 5: r.read_double(); break;
                default: r.read_raw(1 + (op % 40)); break;
            }
        }
    } catch (const std::exception&) {
        // Expected: malformed input is rejected with TlError.
    }
    return 0;
}
