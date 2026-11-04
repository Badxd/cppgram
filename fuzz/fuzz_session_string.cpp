// libFuzzer target: session string decoding from untrusted text.
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

#include "cppgram/session_string.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);
    cppgram::mtproto::SessionData out;
    try {
        cppgram::decode_session_string(text, out);
    } catch (const std::exception&) {
        // Malformed input may throw; it must never crash.
    }
    return 0;
}
