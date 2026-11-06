// Benchmarks for metrics, async execution and media transfers, run against the in-process loopback fake DC.
// They measure what this library does on this machine. They do not contact Telegram and use no credentials.
// Numbers are comparable only between runs on the same machine, with the same build type and compiler flags.
// Each case reports the median, minimum and maximum of several timed samples. Nothing here is a claim about a
// real Telegram server, a real network, or the behaviour of a live account.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "cppgram/async.h"
#include "cppgram/client.h"
#include "cppgram/metrics.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/task.h"

#include "../tests/mtproto/fake_dc.h"

#ifndef CPPGRAM_BENCH_BUILD_TYPE
#    define CPPGRAM_BENCH_BUILD_TYPE "unknown"
#endif

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using cppgram::metrics::Registry;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;

namespace {

constexpr int kSamples = 5;

struct Stats {
    double median_ns = 0;
    double min_ns = 0;
    double max_ns = 0;
};

Stats summarize(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return Stats { v[v.size() / 2], v.front(), v.back() };
}

// Runs `body` kSamples times. Each run is timed once and reported per operation.
template<class F>
Stats time_per_op(std::uint64_t operations, F&& body) {
    std::vector<double> samples;
    for (int s = 0; s < kSamples; ++s) {
        const auto begin = Clock::now();
        body();
        const auto took = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
        samples.push_back(took / static_cast<double>(operations));
    }
    return summarize(samples);
}

void print_row(const char* name, const Stats& s, std::uint64_t operations_per_run) {
    const double ops_per_sec = 1e9 / s.median_ns;
    std::printf("%-46s median %10.1f ns/op  min %10.1f  max %10.1f  (%.3g ops/s, %llu ops/run)\n", name, s.median_ns,
                s.min_ns, s.max_ns, ops_per_sec, static_cast<unsigned long long>(operations_per_run));
}

void print_environment() {
    std::printf("== environment\n");
    std::printf("compiler     : %s\n", __VERSION__);
    std::printf("build type   : %s\n", CPPGRAM_BENCH_BUILD_TYPE);
    std::printf("hardware cpus: %u\n", std::thread::hardware_concurrency());
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            std::printf("cpu model    : %s\n", line.substr(line.find(':') + 2).c_str());
            break;
        }
    }
    std::ifstream meminfo("/proc/meminfo");
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal", 0) == 0) {
            std::printf("memory       : %s\n", line.substr(line.find(':') + 2).c_str());
            break;
        }
    }
    std::printf("\n");
}

// Resident set size in KiB, from /proc/self/statm. Returns 0 when unavailable.
std::uint64_t resident_kib() {
    std::ifstream statm("/proc/self/statm");
    std::uint64_t pages_total = 0;
    std::uint64_t pages_resident = 0;
    statm >> pages_total >> pages_resident;
    return pages_resident * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE)) / 1024;
}

// ---- metrics

void bench_metrics() {
    std::printf("== metrics\n");
    {
        Registry reg;
        auto counter = reg.counter("bench_counter_total", "bench").labels({});
        const std::uint64_t n = 2'000'000;
        print_row("counter inc, 1 thread (enabled)",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i)
                                      counter.inc();
                              }),
                  n);
    }
    {
        Registry reg;
        reg.set_enabled(false);
        auto counter = reg.counter("bench_disabled_total", "bench").labels({});
        const std::uint64_t n = 2'000'000;
        print_row("counter inc, registry disabled",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i)
                                      counter.inc();
                              }),
                  n);
    }
    {
        Registry reg;
        auto gauge = reg.gauge("bench_gauge", "bench").labels({});
        const std::uint64_t n = 2'000'000;
        print_row("gauge set, 1 thread (enabled)",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i)
                                      gauge.set(static_cast<double>(i));
                              }),
                  n);
    }
    {
        Registry reg;
        auto hist = reg.histogram("bench_latency_seconds", "bench", { 0.001, 0.01, 0.1, 1.0 }).labels({});
        const std::uint64_t n = 1'000'000;
        print_row("histogram observe, 1 thread (4 buckets)",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i)
                                      hist.observe(static_cast<double>(i & 1023) * 1e-4);
                              }),
                  n);
    }
    for (int threads : { 2, 4 }) {
        Registry reg;
        auto counter = reg.counter("bench_contended_total", "bench").labels({});
        const std::uint64_t per_thread = 500'000;
        const std::uint64_t n = per_thread * static_cast<std::uint64_t>(threads);
        std::string name = "counter inc, " + std::to_string(threads) + " threads contending";
        print_row(name.c_str(),
                  time_per_op(n,
                              [&] {
                                  std::vector<std::thread> workers;
                                  for (int t = 0; t < threads; ++t) {
                                      workers.emplace_back([&counter, per_thread] {
                                          for (std::uint64_t i = 0; i < per_thread; ++i)
                                              counter.inc();
                                      });
                                  }
                                  for (auto& w : workers)
                                      w.join();
                              }),
                  n);
    }
    std::printf("\n");
}

// ---- async

cppgram::async::Task<int> constant_task(int v) {
    co_return v;
}

void bench_async() {
    std::printf("== async\n");
    {
        // Round trip: post, run, get. One operation per sample is too small to time, so each sample does many.
        cppgram::async::Runner runner;
        const std::uint64_t n = 5000;
        print_row("post + get round trip (lambda)",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i) {
                                      auto r = runner.post([](std::stop_token) {
                                          return 1;
                                      });
                                      (void)r.get();
                                  }
                              }),
                  n);
    }
    {
        cppgram::async::Runner runner;
        const std::uint64_t n = 5000;
        print_row("spawn(Task) + get round trip (coroutine)",
                  time_per_op(n,
                              [&] {
                                  for (std::uint64_t i = 0; i < n; ++i) {
                                      auto r = cppgram::async::spawn(runner, constant_task(1));
                                      (void)r.get();
                                  }
                              }),
                  n);
    }
    {
        // Throughput: post everything first, then drain. Measures queueing plus execution without waiting per item.
        cppgram::async::Runner runner;
        const std::uint64_t n = 50000;
        print_row("executor throughput (batch of 50k posts)",
                  time_per_op(n,
                              [&] {
                                  std::vector<cppgram::async::AsyncResult<int>> results;
                                  results.reserve(n);
                                  for (std::uint64_t i = 0; i < n; ++i) {
                                      results.push_back(runner.post([](std::stop_token) {
                                          return 1;
                                      }));
                                  }
                                  for (auto& r : results)
                                      (void)r.get();
                              }),
                  n);
    }
    {
        // Queue wait: time from releasing a busy worker until the next queued job starts. The job was posted while
        // the blocker was running, so it has been waiting in the queue for the whole blocker's lifetime.
        std::vector<double> waits_us;
        for (int s = 0; s < kSamples; ++s) {
            cppgram::async::Runner runner;
            std::promise<void> release;
            auto gate = release.get_future().share();
            std::promise<void> blocker_started;
            auto blocker_started_future = blocker_started.get_future();
            auto blocker = runner.post([gate, &blocker_started](std::stop_token) {
                blocker_started.set_value();
                gate.wait();
            });
            blocker_started_future.wait();
            std::promise<Clock::time_point> started;
            auto started_future = started.get_future();
            auto queued = runner.post([&started](std::stop_token) {
                started.set_value(Clock::now());
            });
            std::this_thread::sleep_for(5ms); // the queued job is definitely waiting behind the blocker
            const auto released_at = Clock::now();
            release.set_value();
            blocker.get();
            const auto ran_at = started_future.get();
            queued.get();
            waits_us.push_back(std::chrono::duration<double, std::micro>(ran_at - released_at).count());
        }
        const Stats s = summarize(waits_us);
        std::printf("%-46s median %10.1f us       min %10.1f  max %10.1f  (release -> start of next job)\n",
                    "queue wait behind a busy worker", s.median_ns, s.min_ns, s.max_ns);
    }
    {
        // Cancellation overhead: cancel() on an operation that has not started, and a stop request on a finished one.
        cppgram::async::Runner runner;
        const std::uint64_t n = 5000;
        print_row("cancel() before start, then get (per op)",
                  time_per_op(n,
                              [&] {
                                  std::promise<void> release;
                                  auto gate = release.get_future().share();
                                  auto blocker = runner.post([gate](std::stop_token) {
                                      gate.wait();
                                  });
                                  std::vector<cppgram::async::AsyncResult<int>> pending;
                                  pending.reserve(n);
                                  for (std::uint64_t i = 0; i < n; ++i)
                                      pending.push_back(runner.post([](std::stop_token) {
                                          return 1;
                                      }));
                                  for (auto& p : pending)
                                      p.cancel();
                                  release.set_value();
                                  blocker.get();
                                  for (auto& p : pending) {
                                      try {
                                          (void)p.get();
                                      } catch (const cppgram::errors::CancelledError&) { }
                                  }
                              }),
                  n);
    }
    {
        // Concurrent submitters: four threads each post 2500 operations to one runner.
        cppgram::async::Runner runner;
        const std::uint64_t n = 10000;
        print_row("executor, 4 submitter threads (10k ops)",
                  time_per_op(n,
                              [&] {
                                  std::vector<std::thread> submitters;
                                  std::vector<std::vector<cppgram::async::AsyncResult<int>>> results(4);
                                  for (int t = 0; t < 4; ++t) {
                                      submitters.emplace_back([&, t] {
                                          for (int i = 0; i < 2500; ++i) {
                                              results[static_cast<std::size_t>(t)].push_back(
                                                  runner.post([](std::stop_token) {
                                                      return 1;
                                                  }));
                                          }
                                      });
                                  }
                                  for (auto& th : submitters)
                                      th.join();
                                  for (auto& group : results) {
                                      for (auto& r : group)
                                          (void)r.get();
                                  }
                              }),
                  n);
    }
    std::printf("\n");
}

// ---- media (loopback fake DC)

constexpr std::uint32_t kBoolTrue = 0x997275b5u;

cppgram::ClientOptions media_options(cppgram::testing::FakeDc& dc, const std::string& session_path) {
    cppgram::ClientOptions o;
    o.api_id = 1;
    o.api_hash = "00000000000000000000000000000000"; // dummy value: nothing is sent to Telegram
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = { dc.public_key() };
    o.session_path = session_path;
    o.keepalive_ms = 500;
    o.connect_timeout_ms = 2000;
    return o;
}

std::string bench_dir(const std::string& name) {
    static std::atomic<int> counter { 0 };
    const auto dir = std::filesystem::temp_directory_path()
        / ("cppgram-bench-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + "-" + name);
    std::filesystem::create_directories(dir);
    return dir.string();
}

void write_pattern(const std::string& path, std::size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::string block(1 << 20, '\0');
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<char>((i * 7) & 0xff);
    for (std::size_t written = 0; written < size; written += block.size()) {
        const std::size_t chunk = std::min(block.size(), size - written);
        out.write(block.data(), static_cast<std::streamsize>(chunk));
    }
}

void bench_media() {
    std::printf("== media (loopback fake DC; no Telegram)\n");
    // Upload: throughput and sampled resident memory for two file sizes. If upload streams, the RSS peak should not
    // grow with the file size. Only the parts in flight are buffered, and the server side keeps only byte counts.
    for (std::size_t mib : { 8u, 64u }) {
        cppgram::testing::FakeDc dc;
        std::atomic<std::uint64_t> received_bytes { 0 };
        dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
            if (id != fn::upload_saveBigFilePart::kId && id != fn::upload_saveFilePart::kId)
                return false;
            args.read_int64();
            args.read_int32();
            if (id == fn::upload_saveBigFilePart::kId)
                args.read_int32();
            const auto bytes = args.read_bytes();
            received_bytes.fetch_add(bytes.size());
            out.write_uint32(kBoolTrue);
            return true;
        });
        const std::string dir = bench_dir("upload");
        const std::string path = dir + "/upload.bin";
        write_pattern(path, mib << 20);
        cppgram::Client client(media_options(dc, dir + "/s.session"));
        client.connect();

        std::vector<double> throughput_mib_s;
        std::uint64_t peak_rss = 0;
        std::uint64_t base_rss = resident_kib();
        for (int s = 0; s < 3; ++s) {
            const auto begin = Clock::now();
            client.save_file(path, [&](std::int64_t, std::int64_t) {
                peak_rss = std::max(peak_rss, resident_kib());
            });
            const double secs = std::chrono::duration<double>(Clock::now() - begin).count();
            throughput_mib_s.push_back(static_cast<double>(mib) / secs);
        }
        const Stats s = summarize(throughput_mib_s);
        std::printf("upload %3zu MiB (loopback)      median %8.1f MiB/s  min %8.1f  max %8.1f\n", mib, s.median_ns,
                    s.min_ns, s.max_ns);
        std::printf("                               RSS at start %llu KiB, peak sampled during upload %llu KiB"
                    " (server side keeps byte counts only)\n",
                    static_cast<unsigned long long>(base_rss), static_cast<unsigned long long>(peak_rss));
        std::filesystem::remove_all(dir);
    }

    // Download: a fresh transfer, and a real resume. The resume case first fails once at the halfway chunk, which
    // leaves a .part file and its sidecar exactly as an interrupted download does. Only the second call is timed.
    {
        const std::size_t size = 16u << 20;
        std::string content(size, '\0');
        for (std::size_t i = 0; i < size; ++i)
            content[i] = static_cast<char>((i * 13) & 0xff);
        std::atomic<bool> fail_at_half { false };
        cppgram::testing::FakeDc dc;
        dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
            if (id != fn::upload_getFile::kId)
                return false;
            args.read_uint32();
            args.read_uint32();
            args.read_int64();
            args.read_int64();
            args.read_bytes();
            args.read_string();
            const std::int64_t offset = args.read_int64();
            const std::int32_t limit = args.read_int32();
            if (fail_at_half.load() && offset == static_cast<std::int64_t>(size / 2)) {
                fail_at_half = false;
                ty::rpc_error err;
                err.error_code = 400;
                err.error_message = "BENCH_INTERRUPTED";
                err.serialize(out);
                return true;
            }
            const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(offset), content.size());
            const std::size_t end = std::min<std::size_t>(begin + static_cast<std::size_t>(limit), content.size());
            ty::upload_file file;
            file.type = std::make_shared<cppgram::mtproto::tl::storage_FileType>(
                cppgram::mtproto::tl::storage_FileType { ty::storage_fileUnknown {} });
            file.mtime = 0;
            file.bytes.assign(content.begin() + static_cast<std::ptrdiff_t>(begin),
                              content.begin() + static_cast<std::ptrdiff_t>(end));
            cppgram::mtproto::tl::upload_File wrapped { file };
            wrapped.serialize(out);
            return true;
        });
        const std::string dir = bench_dir("download");
        auto options = media_options(dc, dir + "/s.session");
        options.retry.max_attempts = 1;
        cppgram::Client client(options);
        client.connect();
        cppgram::Message msg;
        msg.media.kind = cppgram::MediaKind::Document;
        msg.media.id = 4242;
        msg.media.access_hash = 9;
        msg.media.file_reference = { 1, 2, 3 };
        msg.media.size = static_cast<std::int64_t>(size);

        std::vector<double> fresh;
        for (int s = 0; s < kSamples; ++s) {
            const std::string dest = dir + "/fresh" + std::to_string(s) + ".bin";
            const auto begin = Clock::now();
            client.download_media(msg, dest);
            const double secs = std::chrono::duration<double>(Clock::now() - begin).count();
            fresh.push_back(static_cast<double>(size) / (1u << 20) / secs);
            std::filesystem::remove(dest);
        }
        const Stats f = summarize(fresh);
        std::printf("download 16 MiB fresh (loopback)  median %8.1f MiB/s  min %8.1f  max %8.1f\n", f.median_ns,
                    f.min_ns, f.max_ns);

        std::vector<double> resumed;
        for (int s = 0; s < kSamples; ++s) {
            const std::string dest = dir + "/resume" + std::to_string(s) + ".bin";
            fail_at_half = true;
            try {
                client.download_media(msg, dest);
            } catch (const cppgram::mtproto::RpcError&) {
                // expected: the injected failure leaves the partial file behind
            }
            const auto begin = Clock::now();
            client.download_media(msg, dest);
            const double secs = std::chrono::duration<double>(Clock::now() - begin).count();
            resumed.push_back(static_cast<double>(size / 2) / (1u << 20) / secs); // MiB actually transferred
            std::filesystem::remove(dest);
        }
        const Stats r = summarize(resumed);
        std::printf("download, resumed from 8 MiB (loopback) median %8.1f MiB/s  min %8.1f  max %8.1f"
                    "  (MiB transferred by the resume)\n",
                    r.median_ns, r.min_ns, r.max_ns);
        std::filesystem::remove_all(dir);
    }
    std::printf("\n");
}

} // namespace

int main() {
    print_environment();
    bench_metrics();
    bench_async();
    bench_media();
    return 0;
}
