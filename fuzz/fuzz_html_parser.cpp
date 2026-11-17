// libFuzzer target: HTML and Markdown entity parsers on untrusted message text.
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

#include "cppgram/parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    try {
        (void)cppgram::parse_html(text);
        (void)cppgram::parse_markdown(text);
        (void)cppgram::utf16_length(text);
    } catch (const std::exception&) {
    }
    return 0;
}
