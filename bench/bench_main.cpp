// cppgram micro-benchmarks. Measures what the library does on this machine; it does not contact Telegram.
// Each case runs `kSamples` timed batches. Reported: median, min and max time per operation, and throughput
// where the input size is known. Numbers are only comparable on the same machine and build flags.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/tl.h"
#include "cppgram/mtproto/transport.h"
#include "cppgram/storage.h"

using namespace cppgram;
using namespace cppgram::mtproto;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kSamples = 15;

struct Stats {
    double median_ns, min_ns, max_ns;
};

Stats summarize(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return Stats{v[v.size() / 2], v.front(), v.back()};
}

// Runs `op` in batches so each sample lasts at least ~20 ms, then returns per-operation statistics.
template <typename F>
Stats measure(F&& op, int min_batch = 1) {
    // Warm-up and batch size calibration.
    int batch = min_batch;
    for (;;) {
        const auto t0 = Clock::now();
        for (int i = 0; i < batch; ++i) op();
        const auto dt = std::chrono::duration<double>(Clock::now() - t0).count();
        if (dt >= 0.02 || batch >= (1 << 24)) break;
        batch *= 2;
    }
    std::vector<double> samples;
    samples.reserve(kSamples);
    for (int s = 0; s < kSamples; ++s) {
        const auto t0 = Clock::now();
        for (int i = 0; i < batch; ++i) op();
        const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
        samples.push_back(ns / batch);
    }
    return summarize(samples);
}

void report(const char* name, const Stats& s, std::size_t bytes_per_op = 0) {
    std::printf("%-34s median %12.1f ns  (min %10.1f, max %10.1f)", name, s.median_ns, s.min_ns, s.max_ns);
    if (bytes_per_op > 0) {
        const double mib_s = (static_cast<double>(bytes_per_op) / (1024.0 * 1024.0)) / (s.median_ns * 1e-9);
        std::printf("  %9.1f MiB/s", mib_s);
    }
    std::printf("\n");
}

Bytes pattern(std::size_t n) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(i * 131u + 7u);
    return b;
}

}  // namespace

int main() {
    std::printf("cppgram benchmarks (%d samples per case; median/min/max per operation)\n\n", kSamples);

    // ---- AES-256-IGE and CRC
    for (std::size_t size : {std::size_t{1024}, std::size_t{64 * 1024}}) {
        const Bytes key = pattern(32), iv = pattern(32);
        Bytes data = pattern(size);
        char label[64];
        std::snprintf(label, sizeof label, "aes_ige_encrypt %zu B", size);
        report(label, measure([&] { volatile auto r = aes_ige_encrypt(data, key, iv).size(); (void)r; }), size);
        std::snprintf(label, sizeof label, "aes_ige_decrypt %zu B", size);
        report(label, measure([&] { volatile auto r = aes_ige_decrypt(data, key, iv).size(); (void)r; }), size);
    }
    {
        const Bytes data = pattern(64 * 1024);
        report("crc32 64 KiB", measure([&] { volatile auto r = crc32(data); (void)r; }), data.size());
        report("sha256 64 KiB", measure([&] { volatile auto r = sha256(data).size(); (void)r; }), data.size());
    }

    // ---- MTProto message layer (encrypt_packet / decrypt_packet), 1 KiB inner payload
    {
        const Bytes auth_key = pattern(256);
        InnerMessage msg;
        msg.salt = 0x1122334455667788ull;
        msg.session_id = 0x99aabbccddeeff00ull;
        msg.message_id = 0x5f00000000ull;
        msg.seq_no = 1;
        msg.body = pattern(1024);
        const Bytes plain = serialize_inner(msg);
        const Bytes packet = encrypt_packet(auth_key, plain, true);
        report("encrypt_packet 1 KiB", measure([&] { volatile auto r = encrypt_packet(auth_key, plain, true).size(); (void)r; }), 1024);
        report("decrypt_packet 1 KiB", measure([&] { volatile auto r = decrypt_packet(auth_key, packet, true).size(); (void)r; }), 1024);
    }

    // ---- TL reader/writer: a request-shaped record (int, long, bool, int128, string, bytes)
    {
        const Bytes int128 = pattern(16);
        const std::string text(200, 'x');
        const Bytes blob = pattern(1024);
        auto write_record = [&] {
            TlWriter w;
            w.write_uint32(0x12345678u);
            w.write_int64(-42);
            w.write_bool(true);
            w.write_int128(int128);
            w.write_string(text);
            w.write_bytes(blob);
            return w.take();
        };
        const Bytes encoded = write_record();
        report("tl write record (~1.3 KiB)", measure([&] { volatile auto r = write_record().size(); (void)r; }), encoded.size());
        report("tl read record (~1.3 KiB)", measure([&] {
            TlReader r(encoded);
            volatile auto a = r.read_uint32();
            volatile auto b = r.read_int64();
            volatile auto c = r.read_bool();
            volatile auto d = r.read_int128().size();
            volatile auto e = r.read_bytes().size();
            volatile auto f = r.read_bytes().size();
            (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
        }), encoded.size());
    }

    // ---- transport framing (intermediate), 1 KiB payload
    {
        const Bytes payload = pattern(1024);
        Transport tx(TransportKind::Intermediate);
        report("transport encode 1 KiB", measure([&] { volatile auto r = tx.encode(payload).size(); (void)r; }), 1024);
        const Bytes frame = Transport(TransportKind::Intermediate).encode(payload);
        report("transport feed+next 1 KiB", measure([&] {
            Transport rx(TransportKind::Intermediate);
            rx.feed(frame);
            volatile auto r = rx.next().has_value();
            (void)r;
        }), 1024);
    }

    // ---- session file and sqlite store (file system bound)
    {
        const auto dir = std::filesystem::temp_directory_path() / "cppgram_bench";
        std::filesystem::create_directories(dir);
        SessionData data;
        data.dc_id = 2;
        data.auth_key = pattern(256);
        data.server_salt = 77;
        data.time_offset = -3;
        const std::string file = (dir / "session.bin").string();
        save_session_file(file, data);
        report("session file save (fsync)", measure([&] { save_session_file(file, data); }));
        report("session file load", measure([&] { SessionData out; volatile bool ok = load_session_file(file, out); (void)ok; }));

        const std::string db = (dir / "store.sqlite").string();
        std::filesystem::remove(db);
        SqliteStorage store(db);
        store.save_session(data);
        report("sqlite save_session (FULL sync)", measure([&] { store.save_session(data); }));
        report("sqlite load_session", measure([&] { SessionData out; volatile bool ok = store.load_session(out); (void)ok; }));
        std::filesystem::remove_all(dir);
    }

    std::printf("\nEnvironment is printed by tools/run_benchmarks.sh.\n");
    return 0;
}
