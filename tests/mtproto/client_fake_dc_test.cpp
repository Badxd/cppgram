// cppgram::Client against a loopback fake DC (tests/mtproto/fake_dc.h). Covers unexpected disconnects,
// reconnection, reconnect budget exhaustion, shutdown during reconnect, FLOOD_WAIT retry and cancellation,
// and concurrent stop/disconnect stress. Offline only: no Telegram servers and no credentials are used.
// The api_hash below is a dummy test value.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/cancel.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/session.h"
#include "fake_dc.h"

using namespace std::chrono_literals;
using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::testing::FakeDc;
namespace mtp = cppgram::mtproto;

namespace {

std::string unique_session_path(const char* tag) {
    static std::atomic<int> counter{0};
    const auto dir = std::filesystem::temp_directory_path();
    return (dir / ("cppgram-fakedc-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
                   std::to_string(counter++) + ".session"))
        .string();
}

struct SessionFile {
    std::string path;
    explicit SessionFile(const char* tag) : path(unique_session_path(tag)) {}
    ~SessionFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

ClientOptions options_for(FakeDc& dc, const std::string& session_path) {
    ClientOptions o;
    o.api_id = 1;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = session_path;
    o.keepalive_ms = 150;
    o.debug = std::getenv("CPPGRAM_TEST_DEBUG") != nullptr;
    o.connect_timeout_ms = 2000;
    o.max_reconnects = 3;
    o.retry.max_attempts = 3;
    o.retry.base = std::chrono::milliseconds(10);
    o.retry.cap = std::chrono::milliseconds(50);
    o.retry.max_flood_wait = std::chrono::seconds(60);
    return o;
}

// Waits until `pred` is true or the deadline passes.
template <class Pred>
bool wait_until(Pred pred, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

// Runs client.run() on its own thread. The destructor stops the client and waits for run() to return, so a
// failed assertion cannot leave a running thread behind (std::async futures block in their destructor).
struct Runner {
    explicit Runner(Client& c) : client(c), fut(std::async(std::launch::async, [&c] { c.run(); })) {}
    ~Runner() {
        client.stop();
        if (fut.valid()) fut.wait_for(10s);
    }
    Client& client;
    std::future<void> fut;
};

}  // namespace

TEST(ClientFakeDc, ConnectRunAndStopIsClean) {
    FakeDc dc;
    SessionFile file("clean");
    Client client(options_for(dc, file.path));
    client.connect();
    EXPECT_TRUE(client.is_connected());
    Runner done(client);
    std::this_thread::sleep_for(300ms);
    client.stop();
    ASSERT_EQ(done.fut.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(done.fut.get());
    client.disconnect();
}

TEST(ClientFakeDc, UnexpectedDropIsDetectedAndReconnected) {
    FakeDc dc;
    SessionFile file("drop");
    std::mutex m;
    std::vector<Client::ConnectionEvent> events;
    Client client(options_for(dc, file.path));
    client.on_connection_state([&](Client&, Client::ConnectionEvent e) {
        std::lock_guard<std::mutex> lock(m);
        events.push_back(e);
    });
    client.connect();
    Runner done(client);
    ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() == 1; }, 5s));

    dc.drop_connections();
    ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() == 2 && client.is_connected(); }, 10s));

    client.stop();
    ASSERT_EQ(done.fut.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(done.fut.get());
    std::lock_guard<std::mutex> lock(m);
    ASSERT_GE(events.size(), 3u);
    EXPECT_EQ(events[events.size() - 2], Client::ConnectionEvent::Reconnecting);
    EXPECT_EQ(events.back(), Client::ConnectionEvent::Connected);
}

TEST(ClientFakeDc, RepeatedDropsEachRecover) {
    FakeDc dc;
    SessionFile file("repeat");
    Client client(options_for(dc, file.path));
    client.connect();
    Runner done(client);
    // Each drop is issued only after the client finished the previous reconnect, so every round is a
    // reconnect of an established connection.
    for (int round = 1; round <= 3; ++round) {
        ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() == round && client.is_connected(); }, 5s));
        dc.drop_connections();
        ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() == round + 1 && client.is_connected(); }, 5s));
    }
    client.stop();
    ASSERT_EQ(done.fut.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(done.fut.get());
}

TEST(ClientFakeDc, ReconnectFailureExhaustsBudgetAndRunThrows) {
    FakeDc dc;
    SessionFile file("budget");
    Client client(options_for(dc, file.path));
    client.connect();
    dc.set_refusing(true);
    Runner done(client);
    ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() >= 1; }, 5s));
    dc.drop_connections();
    ASSERT_EQ(done.fut.wait_for(15s), std::future_status::ready);
    EXPECT_THROW(done.fut.get(), mtp::ConnectionError);
    EXPECT_FALSE(client.is_connected());
}

TEST(ClientFakeDc, StopDuringReconnectBackoffReturnsCleanly) {
    FakeDc dc;
    SessionFile file("stopreconnect");
    ClientOptions o = options_for(dc, file.path);
    o.max_reconnects = 50;
    o.retry.base = std::chrono::milliseconds(2000);  // long backoff so stop() interrupts it
    o.retry.cap = std::chrono::milliseconds(4000);
    Client client(o);
    client.connect();
    dc.set_refusing(true);
    Runner done(client);
    ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() >= 1; }, 5s));
    dc.drop_connections();
    std::this_thread::sleep_for(150ms);
    const auto before = std::chrono::steady_clock::now();
    client.stop();
    ASSERT_EQ(done.fut.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(done.fut.get());
    EXPECT_LT(std::chrono::steady_clock::now() - before, 3s);
}

TEST(ClientFakeDc, StoppedClientDoesNotReconnectAfterwards) {
    FakeDc dc;
    SessionFile file("noreconnect");
    Client client(options_for(dc, file.path));
    client.connect();
    Runner done(client);
    ASSERT_TRUE(wait_until([&] { return dc.connections_accepted() == 1; }, 5s));
    // stop() before run() has started is ignored by design, so let run() reach its receive loop first.
    std::this_thread::sleep_for(300ms);
    client.stop();
    ASSERT_EQ(done.fut.wait_for(5s), std::future_status::ready);
    EXPECT_NO_THROW(done.fut.get());
    dc.drop_connections();  // a late drop after an intentional stop must not start a reconnect
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(dc.connections_accepted(), 1);
}

TEST(ClientFakeDc, FloodWaitIsRetriedAfterServerDelay) {
    FakeDc dc;
    SessionFile file("flood");
    Client client(options_for(dc, file.path));
    client.connect();
    dc.set_flood(2, "FLOOD_WAIT_1");
    const auto start = std::chrono::steady_clock::now();
    const auto nearest = client.invoke(cppgram::mtproto::tl::functions::help_getNearestDc{});
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto* dc_info = std::get_if<cppgram::mtproto::tl::types::nearestDc>(&nearest.value);
    ASSERT_NE(dc_info, nullptr);
    EXPECT_EQ(dc_info->country, "US");
    EXPECT_EQ(dc.flood_answers(), 2);
    EXPECT_GE(elapsed, 2s);  // two FLOOD_WAIT_1 answers: never resent before each wait ends
    EXPECT_LT(elapsed, 10s);
}

TEST(ClientFakeDc, FloodWaitBeyondPolicyLimitIsReturnedNotSlept) {
    FakeDc dc;
    SessionFile file("floodlimit");
    ClientOptions o = options_for(dc, file.path);
    o.retry.max_flood_wait = std::chrono::seconds(5);
    Client client(o);
    client.connect();
    dc.set_flood(1, "FLOOD_WAIT_100");
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.invoke(cppgram::mtproto::tl::functions::help_getNearestDc{}), mtp::FloodWaitError);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
    EXPECT_EQ(dc.flood_answers(), 1);
}

TEST(ClientFakeDc, ConsecutiveFloodWaitsAreBoundedByAttempts) {
    FakeDc dc;
    SessionFile file("floodbound");
    ClientOptions o = options_for(dc, file.path);
    o.retry.max_attempts = 2;
    Client client(o);
    client.connect();
    dc.set_flood(10, "FLOOD_WAIT_1");
    EXPECT_THROW(client.invoke(cppgram::mtproto::tl::functions::help_getNearestDc{}), mtp::FloodWaitError);
    EXPECT_EQ(dc.flood_answers(), 2);  // attempt budget spent: no further resends
}

TEST(ClientFakeDc, StopCancelsAFloodWaitInProgress) {
    FakeDc dc;
    SessionFile file("floodcancel");
    Client client(options_for(dc, file.path));
    client.connect();
    dc.set_flood(1, "FLOOD_WAIT_30");
    std::thread stopper([&client] {
        std::this_thread::sleep_for(150ms);
        client.stop();
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.invoke(cppgram::mtproto::tl::functions::help_getNearestDc{}), mtp::OperationCancelled);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
    stopper.join();
    EXPECT_EQ(dc.flood_answers(), 1);  // the request was not resent after cancellation
}

TEST(ClientFakeDc, ConcurrentStopAndDropStress) {
    // Each round races run() against drop_connections() and stop() on other threads. stop() is repeated until
    // run() returns, because a stop() that lands before run() has started is ignored by design. The only
    // acceptable outcomes are a clean return from run() or the reconnect budget being spent (ConnectionError).
    std::mt19937 rng(12345);
    for (int round = 0; round < 12; ++round) {
        FakeDc dc;
        SessionFile file("stress");
        Client client(options_for(dc, file.path));
        client.connect();
        std::atomic<bool> finished{false};
        std::promise<void> promise;
        auto result = promise.get_future();
        std::thread runner([&] {
            try {
                client.run();
                promise.set_value();
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
            finished = true;
        });

        const auto drop_delay = std::chrono::milliseconds(rng() % 30);
        const auto stop_delay = std::chrono::milliseconds(rng() % 30);
        std::thread dropper([&] {
            std::this_thread::sleep_for(drop_delay);
            dc.drop_connections();
        });
        std::thread stopper([&] {
            std::this_thread::sleep_for(stop_delay);
            for (int i = 0; i < 3000 && !finished; ++i) {
                client.stop();  // concurrent stop() calls must be safe
                std::this_thread::sleep_for(5ms);
            }
        });
        std::thread stopper2([&] {
            for (int i = 0; i < 3000 && !finished; ++i) {
                client.stop();
                std::this_thread::sleep_for(7ms);
            }
        });
        dropper.join();
        ASSERT_EQ(result.wait_for(15s), std::future_status::ready) << "round " << round;
        stopper.join();
        stopper2.join();
        runner.join();
        try {
            result.get();
        } catch (const mtp::ConnectionError&) {
            // Acceptable only when the drop and the reconnect budget raced; the run still ended.
        }
        client.disconnect();
    }
}
