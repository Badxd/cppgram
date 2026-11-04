# Fuzzers

Four libFuzzer targets. Each feeds untrusted bytes into one parser or decryption path. Malformed input must throw a
documented exception or return an error. It must never crash, hang, trigger a sanitizer report, or allocate beyond the
documented bounds.

| Target | Input | Entry point | Expected rejections |
|---|---|---|---|
| `fuzz_tl_reader` | raw TL data from the server | `TlReader` primitives | `TlError`, `std::exception` |
| `fuzz_session_string` | session string text | `decode_session_string` | `std::exception` |
| `fuzz_html_parser` | message text | `parse_html`, `parse_markdown`, `utf16_length` | `std::exception` |
| `fuzz_session_crypto` | encrypted session blob (CGE1) | `is_encrypted`, `detail::validate_header`, `detail::open_with_key`, sampled `decrypt` | `std::exception` |

## Build

Clang with libFuzzer and the ASan runtime is required:

    cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 -DBUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build-fuzz -j1 --target fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto

Or use `tools/run_fuzz_long.sh <seconds>`, which builds these four targets and runs each for the given time.

## Running

    ./build-fuzz/fuzz_tl_reader corpus/tl -max_total_time=600 -timeout=10
    ./build-fuzz/fuzz_session_crypto corpus/cge1 -max_total_time=600 -timeout=10

`-timeout=10` makes a hang a failure. Pass the corpus directory first; libFuzzer writes new interesting inputs there.
Keep the seeds in a copy of the directory if you do not want new inputs added to the seed set.

## Seed corpora

`fuzz/make_session_seeds.cpp` writes the seed files. It is not part of the CMake targets. Build it by hand against the
Release library:

    clang++-18 -std=c++20 -O1 -Iinclude -Isrc fuzz/make_session_seeds.cpp build/Release/libcppgram.a -lssl -lcrypto -lsqlite3 -lpthread -lz -o make_session_seeds
    ./make_session_seeds corpus/seeds

It writes:

- `seed_0` to `seed_7`: valid session strings for `fuzz_session_string` (random keys, not real accounts).
- `cge1_seed_0` to `cge1_seed_3`: valid CGE1 blobs encrypted with the passphrase `fuzz-passphrase`.
- `invalid_*`: known-bad CGE1 variants. Bad magic, parameters below and above the accepted range, zero `r` or `p`,
  truncated header, truncated tag, a flipped tag or ciphertext byte, a trailing byte, and an empty file.

Copy `cge1_seed_*` and `invalid_*` into a corpus directory for `fuzz_session_crypto`. Copy `seed_*` into one for
`fuzz_session_string`.

## CEG1 harness: what it covers

Each input goes through these steps:

1. `is_encrypted` and the header validator, on the raw bytes. This covers size, magic, and the scrypt parameter bounds.
2. The authenticated open (AES-256-GCM, header as AAD) with a key for the input's own salt and parameters. The harness
   caches the derived key per (salt, log2N, r, p), so inputs that keep the seed's salt skip scrypt.
3. The public `decrypt()` on 1 in 64 inputs (chosen by size), so the passphrase-to-key path is exercised too.

Limits, stated plainly:

- Scrypt costs about 30 ms per new salt and parameter set, so the harness runs at about 27 inputs per second when the
  salt changes often. Throughput is low on purpose. It is not a bug.
- The cache means mutations that change the salt or parameters pay for a new derivation. Mutations that change only
  the ciphertext, the tag, or the nonce are cheap.
- Coverage is limited to the code reachable from one fixed passphrase. Other passphrases exercise the same code, but
  the harness does not try them.
- The harness never weakens validation. It calls the same functions `decrypt()` calls.

## Expected rejections and findings

Expected rejections are caught in the harness. Anything else is a finding: a crash, a sanitizer report, a hang past the
timeout, or a successful decryption of data the harness did not produce. Keep the input file and add a regression test in
`tests/mtproto/session_crypto_test.cpp` or the matching parser test before fixing the bug.
