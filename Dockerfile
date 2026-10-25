FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    libboost-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Optimizations are disabled to keep this connectivity-test image quicker to build.
RUN cmake -S . -B build/Release \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS_RELEASE="-O0 -DNDEBUG" \
      -DBUILD_LIVE_CHECK=ON \
    && cmake --build build/Release \
      --target cppgram_live_check \
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

CMD ["/app/cppgram_live_check"]
