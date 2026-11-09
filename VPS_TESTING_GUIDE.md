# VPS testing guide

This guide is the test plan for the owner's Ubuntu 24.04 VPS. The archive's build and test results are in `BUILD_RESULTS.md` and
`TEST_RESULTS.md`. Anything those files do not mark as run has not been run. Each step is labelled by what it touches:

- **OFFLINE**: no network at all.
- **FAKE-DC**: a scripted fake Telegram server in the test process. No real Telegram.
- **LOCAL NETWORK**: loopback sockets only, opened by the test itself.
- **LIVE**: real Telegram, your credentials, your account. Owner only. The archive build never runs these.
- **ARM64**: a check that needs an ARM64 machine. Not run by the archive build.

Do not treat any step as passed until you have run it and read its output. The archive is not a claim that CppGram runs
within 1 GB of RAM. No measurement exists yet. The largest single compile needs about 2.8 GB. Steps E, F, J and K record
the numbers you need.

Put credentials in your shell environment only. Never write them into a file, a command line, a log, or a chat message.

## A. Check the machine [OFFLINE]

```bash
lsb_release -a
uname -r
nproc
free -h
df -h .
gcc --version | head -1
clang --version | head -1
cmake --version | head -1
ninja --version
```

Record the output with the results. Memory and CPU count decide the job counts in steps E and F.

## B. Install dependencies [OFFLINE]

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build ccache pkg-config \
    libboost-dev libcurl4-openssl-dev nlohmann-json3-dev libssl-dev zlib1g-dev \
    libsqlite3-dev libgtest-dev libgmock-dev
```

The configure step checks the minimums: Boost 1.65.1, libcurl 7.58.0 and nlohmann_json 3.7.3, plus OpenSSL, zlib and SQLite3.
GoogleTest is needed only when `ENABLE_TESTS=ON`. In the session, a configure-only run with CMake 3.28.3 found Boost 1.83.0,
libcurl 8.5.0, nlohmann_json 3.11.3, OpenSSL 3.0.13, SQLite 3.45.1 and GTest 1.14.0. Your VPS may differ.

## C. Unpack the archive and verify its hashes [OFFLINE]

```bash
mkdir -p ~/cppgram && cd ~/cppgram
unzip -q /path/to/cppgram-final-ready-for-vps.zip
cd cppgram-final-ready-for-vps
sha256sum -c SHA256SUMS.txt
```

Every line must say `OK`. If one fails, stop and report it. Do not build from a damaged archive.

## D. Configure only [OFFLINE]

```bash
cmake -S . -B /tmp/cppgram-configure -G Ninja -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
rm -rf /tmp/cppgram-configure
```

This proves the CMake files parse and the dependencies are found. It compiles nothing.

## E. Release build with the test suite [OFFLINE]

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
cmake --build build -j2
```

On a 1 GB machine, first add swap and build with one job. The build is the largest memory user. On the release
machine (8 GB), the largest single compile is now `src/mtproto/schema/api_impl_17.cpp`, at about 2.8 GB peak RSS. Before
the file split it was `api_impl_8.cpp` at about 4 GB (`BUILD_RESULTS.md` section 3). That is still more than 1 GB, so a
1 GB machine is not verified: the swap below is a starting point, not a proven fit. Record the real peak here:

```bash
sudo fallocate -l 4G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
cmake --build build -j1
```

Measure the build with the time command, so the peak is recorded rather than assumed. On Ubuntu, `time` may need installing
(`sudo apt-get install -y time`); the archive's build used a Python launcher that reads the child's peak RSS instead:

```bash
/usr/bin/time -v cmake --build build -j1 2> build-time.txt
grep -E "Maximum resident|Elapsed" build-time.txt
```

Record the peak resident size, the elapsed time, and the swap in use (`swapon --show`). Compare the peak with the
per-file figures in `BUILD_RESULTS.md` section 3b. If the build peak is far above 2.8 GB, a different process is using
memory, and the result should be recorded as such.

## F. Run the test suite with CTest [OFFLINE, FAKE-DC and LOCAL NETWORK]

```bash
cd build && ctest -j1 --output-on-failure 2>&1 | tee ../ctest-all.txt; cd ..
grep -E "tests passed|tests failed|Failed" ctest-all.txt
```

Use `-j1` on small machines. Most suites are offline or fake-DC. The `ConnectionNet` suite opens loopback sockets, so it
is LOCAL NETWORK. Record the `Maximum resident` line too:

```bash
/usr/bin/time -v build/tests/cppgram_mtproto_test 2> mtproto-all-time.txt
grep -E "Maximum resident|Elapsed" mtproto-all-time.txt
```

## G. Warnings build [OFFLINE]

```bash
cmake -S . -B build-warn -G Ninja -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build-warn -j1 2>&1 | tee warnings.txt
grep -c "warning:" warnings.txt
```

The project target already sets `-Wall`. This step adds `-Wextra` and `-Wpedantic`. Warnings from the generated schema
files (`src/mtproto/schema/api_impl_*.cpp`) are not project code. List them separately in your report.

## H. Install target [OFFLINE]

```bash
cmake --install build --prefix "$HOME/cppgram-install"
find "$HOME/cppgram-install" -maxdepth 2 | sort | head -40
```

The install puts the library, the public headers and the CMake package files under the prefix. Check that the headers
are present.

## I. Minimal API example, no network [OFFLINE]

`examples/minimal_api/minimal_api.cpp` builds a `Client` from your credentials and stops. It makes no network call.

```bash
cmake -S . -B build-example -G Ninja -DBUILD_MINIMAL_EXAMPLE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-example --target cppgram_minimal_api -j1
export TG_API_ID=<your api id>
export TG_API_HASH=<your api hash>
./build-example/cppgram_minimal_api
```

Expected output: `client constructed; no connection made`. If `TG_API_ID` or `TG_API_HASH` is missing, the program prints
`set TG_API_ID and TG_API_HASH` and exits 1. Use the values from your own my.telegram.org application. Do not write them
into a file or a chat.

## J. Memory of the fake-DC suites [FAKE-DC]

Run the upload and inline suites on their own and record each peak. They show the cost of the test process, not of a
long-running bot.

```bash
for f in EphemeralUploadFakeDc EphemeralInlineFakeDc InlineSendFakeDc InlineDecodeFakeDc RichBlocksFakeDc KeyboardRoundTrip; do
  /usr/bin/time -v build/tests/cppgram_mtproto_test --gtest_filter="${f}.*" 2> "mem-${f}.txt"
  grep -E "Maximum resident" "mem-${f}.txt"
done
```

Run each one three times and keep all three numbers. One run is not a memory profile. Record the VPS size, the kernel and
the archive SHA-256.

## K. ASan and UBSan [OFFLINE]

```bash
tools/run_sanitizers.sh
```

The script builds into `build-san/` with `-O0 -g0 -fsanitize=address,undefined`, then runs ctest with `halt_on_error=1`.
Its own header says the `-j1` build needs about 8 GB of RAM and about 1.5 hours on a small VPS. A 1 GB machine is not the
right place for this step. Run it on a larger machine, or skip it and say so in your report.

## L. ThreadSanitizer [OFFLINE]

```bash
tools/run_tsan.sh
```

The script builds `cppgram_mtproto_test` with `-fsanitize=thread` into `build-tsan/` and runs the concurrency tests first.
It needs about the same memory as step K.

## M. Framing, obfuscation, proxy secret and SOCKS5 tests [OFFLINE]

```bash
build/tests/cppgram_mtproto_test --gtest_filter='TransportFraming*:Obfuscated*:ProxySecret*:Socks5*'
```

These cover intermediate and padded framing, the obfuscated transport, the proxy secret format, and SOCKS5 handshakes.
They run against in-memory data and fake servers. They do not contact a proxy.

## N. Fake-DC suites [FAKE-DC]

```bash
build/tests/cppgram_mtproto_test --gtest_filter='*FakeDc*:KeyboardRoundTrip.*'
```

This covers the suites added or changed in the final pass: `EphemeralUploadFakeDc`, `InlineSendFakeDc`,
`InlineDecodeFakeDc`, `RichBlocksFakeDc`, `EphemeralInlineFakeDc`, `PartialMethodsFakeDc`, `FoldersAuditFakeDc` and
`KeyboardRoundTrip`, plus the
older `*FakeDc` suites. The fake DC answers each request from a handler written in the test. A pass means the client sent
the request the test expects and decoded the scripted answer. It does not mean Telegram accepts the request.

## O. Live login with your bot [LIVE]

Only the owner runs this. Build the live runner, set the variables in your shell, and run one test at a time:

```bash
cmake -S . -B build-live -G Ninja -DBUILD_LIVE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-live --target cppgram_live_tests -j1
export TG_API_ID=<your api id>
export TG_API_HASH=<your api hash>
export TG_BOT_TOKEN=<your bot token>
export TG_LIVE_DIR="$HOME/cppgram-live"
mkdir -p "$TG_LIVE_DIR"
./build-live/cppgram_live_tests padded_framing
```

Read `CPPGRAM_LIVE_TEST_GUIDE.md` and `guides/LIVE_TESTS.md` first. They list the expected output and the exit codes
(0 PASS, 1 FAIL, 2 usage error, 3 SKIP, 4 NOT_SUPPORTED). A SKIP means nothing was tested. Phone login with an OTP is not
in the runner. Test it by hand with a throwaway account.

## P. Encrypted session restore and reconnect [LIVE]

```bash
export CPPGRAM_SESSION_PASSPHRASE=<your passphrase>
./build-live/cppgram_live_tests session_restore
./build-live/cppgram_live_tests reconnect
```

- **session_restore**: saves an encrypted session (the file must start with the `CGE1` format), refuses a wrong
  passphrase with `SessionCorruptError`, and reopens the file in a new client. The new client must keep the same auth key
  with no new handshake, and `get_me` must work. The session file is written to `TG_LIVE_DIR`.
- **reconnect**: connects through a relay on `127.0.0.1` that forwards bytes unchanged to Telegram. The test closes the
  relay for 4 seconds to drop the connection, reopens it, and expects a `Reconnecting` event, a second `Connected` event
  within 90 seconds, and a working `get_me`.

Neither test covers DC migration. PHONE_MIGRATE, NETWORK_MIGRATE and USER_MIGRATE handling is in `src/dc.cpp`. It has no
runner test and no dedicated offline test. The QR-login fake-DC tests touch the migrate path only indirectly. Treat DC
migration as untested until you have a test that targets it.

## Q. SOCKS5, MTProto proxy, padded framing and flood wait [LIVE]

Each line is one run. Record the exit code and the test's output line. Do not run `all` on your main account.

```bash
export TG_SOCKS5_HOST=<proxy host> TG_SOCKS5_PORT=<proxy port>
./build-live/cppgram_live_tests socks5

export TG_MTPROTO_PROXY_HOST=<proxy host> TG_MTPROTO_PROXY_PORT=<proxy port> TG_MTPROTO_PROXY_SECRET=<secret>
./build-live/cppgram_live_tests mtproto_proxy
./build-live/cppgram_live_tests padded_framing

./build-live/cppgram_live_tests flood_wait
```

- **socks5**: needs `TG_SOCKS5_HOST` and `TG_SOCKS5_PORT`. User and password (`TG_SOCKS5_USER`, `TG_SOCKS5_PASS`) are
  optional and must be set together.
- **mtproto_proxy**: validates the secret first. A fake-TLS `ee` secret gives NOT_SUPPORTED with no network use. Plain (32
  hex) and `dd` secrets run the connection.
- **padded_framing**: needs a `dd` secret, otherwise SKIP. PASS shows the client completes `get_me` through the proxy with
  padded-intermediate framing. It does not prove the server pads its frames.
- **flood_wait**: makes only ordinary calls. It does not send extra requests to provoke a limit. If Telegram sends no
  FLOOD_WAIT, the result is SKIP, not PASS. If one does arrive with a long wait, stop and wait the time it gives. Do not
  retry in a loop.

Secrets are read from the environment and never printed.

## R. Collect results [OFFLINE]

Keep these files and no others:

- `ctest-all.txt`, `warnings.txt`, `build-time.txt`, `mtproto-all-time.txt`, and the `mem-*.txt` files from step J.
- The output lines and exit codes of the live runs from steps O to Q.
- The output of `uname -a`, `free -h`, `nproc`, and the archive SHA-256 from step C.

Before you share any file, search it for credential shapes:

```bash
grep -nE "TG_API_HASH|TG_BOT_TOKEN|[0-9]{6,}:[A-Za-z0-9_-]{30,}|CPPGRAM_SESSION_PASSPHRASE" *.txt || echo "no matches"
```

Delete `*.session` files and anything under `TG_LIVE_DIR` before you copy results off the VPS. They contain authorization
keys. Unset the variables when you finish: `unset TG_API_HASH TG_BOT_TOKEN CPPGRAM_SESSION_PASSPHRASE`.

## S. ARM64 [ARM64]

On an ARM64 machine (not the x86_64 VPS unless it is ARM64), run steps A, B, E and F. Record `uname -m` and whether the
build and the offline suites pass. Nothing in the archive has been built on ARM64.

## Files this archive does not include

`build/`, `build-*`, session files, SQLite databases, credentials, binaries and temporary files are excluded from the
release archive. Create them on your machine.

## Before you start: local pre-VPS checks (no VPS, no Telegram)

These checks run on any Linux machine with the build tools. They use no network, no Telegram account and no credentials.

- `make checks`: CMake source lists, version wiring, the format allowlist, the public API index and example syntax.
- `make schema-check`: regenerates the TL code and confirms it matches the checked-in files.
- `make package`: builds the source zip into `dist/`. The packager refuses to include build directories, sessions,
  databases, logs, keys and `.env` files.
- `make verify-release`: checks a release zip against its bundled `SHA256SUMS.txt`, its file list and its forbidden paths.

These checks do not replace steps E to Q below. The VPS and live Telegram results are still NOT RUN until you record them.
GitHub Actions workflows were written but not executed. The live workflow is manual and needs secrets you configure.
