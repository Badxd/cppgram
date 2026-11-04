# Benchmarks

These numbers were measured in one sandbox. They describe local work only (crypto, framing, TL, file and
SQLite I/O). They do not measure network latency, Telegram server time, or RPC round trips, because the
sandbox cannot reach Telegram's data centres.

Do not compare these numbers with another machine without re-running the benchmark there.

## Environment (this run)

| Item | Value |
|---|---|
| Date (UTC) | 2026-10-31 |
| OS | Ubuntu 24.04.5 LTS, Linux 6.18.44 (virtual machine) |
| CPU | Intel Xeon Processor @ 2.10 GHz, 2 threads |
| Memory | 7.8 GiB |
| Compiler | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| OpenSSL | 3.0.13 |
| File system | ext4 (session file and SQLite store) |
| Build (optimized) | `g++ -std=c++20 -O2 -DNDEBUG`, cppgram sources linked directly (see method) |
| Build (as configured) | CMake Release with the cache value `CMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG` |

**Important:** the CMake build directory used for this session had `-O0` in its Release flags, so a plain
`cmake --build` in that directory produces unoptimized code. The `-O2` numbers below come from a direct
build of the same sources at `-O2`. Use a fresh build directory without that cache value for any real
measurement. `tools/run_benchmarks.sh` prints the flags it finds so this is visible in the output.

## Method

- `bench/bench_main.cpp`: each case is calibrated to run at least 20 ms per batch, then 15 timed batches
  are taken. Reported: median, min and max time per operation. Throughput is the input size divided by the
  median.
- No allocation counts are reported. The harness does not count allocations, so that metric is not measured.
- Not measured: connection startup, RPC dispatch latency, update dispatch throughput, media chunk
  processing, latency under load. These need a live server or a mock transport and are not done.

## Results, -O2 (median per operation)

| Case | Median | Min | Max | Throughput |
|---|---:|---:|---:|---:|
| aes_ige_encrypt 1 KiB | 4.22 µs | 3.81 µs | 5.80 µs | 231 MiB/s |
| aes_ige_decrypt 1 KiB | 4.11 µs | 3.86 µs | 8.30 µs | 238 MiB/s |
| aes_ige_encrypt 64 KiB | 208 µs | 202 µs | 230 µs | 300 MiB/s |
| aes_ige_decrypt 64 KiB | 218 µs | 205 µs | 296 µs | 287 MiB/s |
| crc32 64 KiB | 196 µs | 187 µs | 209 µs | 319 MiB/s |
| sha256 64 KiB | 51 µs | 48 µs | 59 µs | 1222 MiB/s |
| encrypt_packet 1 KiB | 10.3 µs | 9.9 µs | 15.8 µs | 95 MiB/s |
| decrypt_packet 1 KiB | 8.2 µs | 7.6 µs | 9.0 µs | 119 MiB/s |
| TL write, ~1.3 KiB record | 359 ns | 355 ns | 446 ns | 3361 MiB/s |
| TL read, ~1.3 KiB record | 147 ns | 140 ns | 219 ns | 8218 MiB/s |
| transport encode 1 KiB | 89 ns | 86 ns | 135 ns | 10943 MiB/s |
| transport feed + next 1 KiB | 50 ns | 49 ns | 71 ns | 19427 MiB/s |
| session file save (write, fsync, rename) | 558 µs | 473 µs | 955 µs | n/a |
| session file load | 4.1 µs | 3.8 µs | 6.4 µs | n/a |
| sqlite save_session (synchronous=FULL) | 154 µs | 116 µs | 365 µs | n/a |
| sqlite load_session | 5.1 µs | 4.8 µs | 5.9 µs | n/a |

## Results, as configured (-O0)

Same harness, built with the `-O0` Release cache value. Shown so the difference is visible; not a target.

| Case | Median (-O0) | Median (-O2) | Ratio |
|---|---:|---:|---:|
| aes_ige_encrypt 1 KiB | 20.9 µs | 4.22 µs | 5.0x |
| aes_ige_encrypt 64 KiB | 1.22 ms | 208 µs | 5.9x |
| crc32 64 KiB | 549 µs | 196 µs | 2.8x |
| encrypt_packet 1 KiB | 53.0 µs | 10.3 µs | 5.1x |
| decrypt_packet 1 KiB | 32.5 µs | 8.2 µs | 4.0x |
| TL write record | 3.57 µs | 359 ns | 9.9x |
| transport encode 1 KiB | 656 ns | 89 ns | 7.4x |
| session file save | 612 µs | 558 µs | 1.1x (I/O bound) |
| sqlite save_session | 185 µs | 154 µs | 1.2x (I/O bound) |

## What the numbers say

- Crypto dominates the message layer: `encrypt_packet` at 1 KiB takes about 10 µs at -O2, of which AES-IGE is
  about 4 µs and SHA-256 key derivation and framing make up most of the rest. That is an acceptable cost
  for a client that sends a few requests per second.
- CRC-32 at 319 MiB/s is the slowest hash-like step per byte and is used only for session files. If session
  files ever become large, switching to a table-driven or hardware-accelerated CRC is a candidate. It is not
  a bottleneck today.
- Session and SQLite writes cost about 0.15–0.6 ms each and are I/O bound (fsync). Writing a session on every
  update would be wasteful; the library writes it at checkpoints.

## Optimization candidates (not done)

These are candidates from the numbers above. None is applied, because none is shown to matter for a real
workload yet:

1. CRC-32 via a table or zlib (about 2–3x on this machine).
2. AES-IGE per-block overhead: the IGE chaining runs block by block; batching could help (measure first).
3. Reduce copies in `encrypt_packet` / `decrypt_packet` (each call builds new `Bytes`).

## Reproduce

```sh
tools/run_benchmarks.sh            # configures a Release build with ENABLE_BENCHMARKS=ON, builds, runs
```

Use a build directory without the `-O0` cache value. Keep the environment block that the script prints
with any numbers you publish.
