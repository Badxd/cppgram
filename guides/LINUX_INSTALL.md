# Linux install and build

## Tested environment

Only the environment below was built and tested. Other distributions and compiler versions are not claimed.

| Item | Tested value |
|---|---|
| OS | Ubuntu 24.04.5 LTS (kernel 6.18, virtual machine, 2 vCPU, 7.8 GiB RAM) |
| Compiler | g++ 13.3.0 (C++20) |
| CMake | 3.28.3 |
| OpenSSL | 3.0.13 (libssl-dev 3.0.13-0ubuntu3.16) |
| SQLite | 3.45.1 (libsqlite3-dev) |
| Boost | 1.83.0 (libboost-dev) |
| libcurl | 8.5.0 (libcurl4-openssl-dev) |
| zlib | 1.3 (zlib1g-dev) |
| nlohmann-json | 3.11.3 (nlohmann-json3-dev) |
| GoogleTest | 1.14.0 (libgtest-dev); tests only |

Clang 18 was used only to build the libFuzzer targets (see `fuzz/README.md`).

## Packages (Ubuntu 24.04)

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake git \
    libssl-dev libsqlite3-dev zlib1g-dev libcurl4-openssl-dev \
    libboost-dev nlohmann-json3-dev libgtest-dev
```

`libgtest-dev` is needed only with `-DENABLE_TESTS=ON`. Debian 12 ships older versions of several of these;
check the versions against the minimums in `CMakeLists.txt` (Boost 1.65.1, CURL 7.58.0, nlohmann_json 3.7.3)
before building there. Debian and other distributions are not tested.

## Build

```sh
git clone <your fork or the repository>
cd cppgram
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Memory: the generated Telegram schema files (`src/mtproto/schema/api_impl_*.cpp`) are large. On a machine
with about 8 GiB of RAM, use `-j2` or lower. The build directory in this environment was built with
`-j2` and an `-O0` Release cache value, which was slower but fit in memory. Do not use `-O0` for real
deployments; see `guides/BENCHMARKS.md`.

Optional targets:

| Option | Target | Notes |
|---|---|---|
| `-DENABLE_TESTS=ON` | `cppgram_mtproto_test`, `cppgram_bot_test` | GoogleTest. `ctest` runs both. |
| `-DENABLE_BENCHMARKS=ON` | `cppgram_bench` | Local work only. |
| `-DBUILD_LIVE_CHECK=ON` | `cppgram_live_check` | Contacts Telegram. Needs credentials in the environment. |
| `-DBUILD_FUZZERS=ON` | fuzz targets | Requires clang with libFuzzer. |
| `-DENABLE_SANITIZERS=ON` | ASan and UBSan build | Use with `-O1` or higher. |
| `-DBUILD_DOCUMENTATION=ON` | Doxygen docs | Requires doxygen. |

## Install and use from CMake

```sh
cmake --install build --prefix /opt/cppgram
```

Downstream `CMakeLists.txt`:

```cmake
find_package(cppgram REQUIRED)
target_link_libraries(my_service PRIVATE cppgram::cppgram)
```

The installed package is not tested in this repository (the install step was not run here). Treat it as
unverified until you have built a downstream project against it.

## Checks that were run

- `tools/check_examples.sh`: every example is syntax-checked against the public headers (no link, no network).
- `tools/check_schema.sh`: the generated TL code is byte-identical when regenerated.
- `ctest` in the Release tree: all tests pass (see `AUDIT.md` for the count and date).
