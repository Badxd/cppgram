# Fuzzers

Three libFuzzer targets. They feed untrusted bytes into the parsers and must never crash or trip ASan.

| Target | Input | Entry point |
|---|---|---|
| `fuzz_tl_reader` | raw TL data from the server | `TlReader` primitives |
| `fuzz_session_string` | session string text | `decode_session_string` |
| `fuzz_html_parser` | message text | `parse_html`, `parse_markdown`, `utf16_length` |

Build (clang with libFuzzer and ASan runtime):

    cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DBUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build-fuzz -j1 --target fuzz_tl_reader fuzz_session_string fuzz_html_parser
    ./build-fuzz/fuzz_tl_reader corpus/tl -max_total_time=600

Expected rejections (`TlError`, `std::runtime_error`) are caught in the harness; anything else is a finding.
