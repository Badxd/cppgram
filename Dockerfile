# Live-test image. The apt layer is the dependency layer: it is cached and only rebuilt when this file changes.
# Source is copied after it, so editing C++ does not reinstall packages.
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    ninja-build \
    libboost-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Optimizations are disabled: live tests are network-bound and the image should build quickly.
# Ninja builds in parallel. Only the live binaries are built; the test suite and fuzzers are not.
RUN cmake -S . -B build/Release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS_RELEASE="-O0 -DNDEBUG" \
      -DBUILD_LIVE_CHECK=ON \
      -DBUILD_LIVE_TESTS=ON \
      -DCPPGRAM_USE_CCACHE=OFF \
    && cmake --build build/Release \
      --target cppgram_live_check cppgram_live_tests \
      --parallel 2

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libsqlite3-0 \
    libssl3 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
COPY --from=builder /src/build/Release/cppgram_live_check /app/cppgram_live_check
COPY --from=builder /src/build/Release/cppgram_live_tests /app/cppgram_live_tests

# Default: the connectivity check, as before. For the six live tests run, for example:
#   heroku run --type=worker --app APP_NAME /app/cppgram_live_tests session_restore
CMD ["/app/cppgram_live_check"]
