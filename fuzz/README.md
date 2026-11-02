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

## Seed corpus for the session string target

`make_session_seeds.cpp` writes valid session strings (random keys, not real accounts) so the libFuzzer run reaches
the body parser instead of failing at the base64 step:

    clang++ -std=c++20 -O1 -Iinclude -Isrc fuzz/make_session_seeds.cpp build/Release/libcppgram.a -lcrypto -lsqlite3 -lz -o make_session_seeds
    ./make_session_seeds corpus/session_string
    ./fuzz_session_string corpus/session_string -max_total_time=120 -timeout=10
