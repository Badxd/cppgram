// In-flight RPC cancellation, tested without Telegram. Two layers are covered:
//  - MtSession against a scripted in-memory channel that can stall, answer, or deliver stale replies;
//  - Connection against a silent loopback TCP peer, to check the real poll-based wait.
// Nothing here connects to a Telegram server.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include "cppgram/async.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/cancel.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"

using namespace cppgram::mtproto;
using namespace std::chrono_literals;
namespace ty = cppgram::mtproto::tl::types;

namespace {

Bytes rpc_result_packet(std::uint64_t req_msg_id, const Bytes& result) {
    TlWriter w;
    w.write_uint32(ty::rpc_result::kId);
    w.write_uint64(req_msg_id);
    w.write_raw(result);
    return w.take();
}

// Encrypted channel whose server is fully scripted by the test. In Echo mode each request gets an rpc_result that
// echoes its body. In Silent mode nothing is answered. A stale reply can be queued ahead of the next answer.
class ScriptedServer : public PacketChannel {
public:
    explicit ScriptedServer(Bytes key)
        : key_(std::move(key)) {
    }

    void set_echo(bool echo) {
        std::lock_guard<std::mutex> lock(mutex_);
        echo_ = echo;
    }
    // Queues an rpc_result for an earlier request. It is delivered before the answer to the next request.
    void queue_stale_reply(std::uint64_t request_id, Bytes body) {
        std::lock_guard<std::mutex> lock(mutex_);
        stale_.emplace_back(request_id, std::move(body));
    }
    std::uint64_t last_request_id() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_request_id_;
    }
    int sends() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sends_;
    }

    void send_packet(const Bytes& packet) override {
        const Bytes plain = decrypt_packet(key_, packet, true);
        const InnerMessage inner = parse_inner(plain);
        std::lock_guard<std::mutex> lock(mutex_);
        if ((inner.seq_no & 1u) == 0)
            return; // acknowledgements get no reply
        ++sends_;
        last_request_id_ = inner.message_id;
        for (auto& [request_id, body] : stale_) {
            replies_.push_back(encrypt_reply(inner, rpc_result_packet(request_id, body)));
        }
        stale_.clear();
        if (echo_) {
            replies_.push_back(encrypt_reply(inner, rpc_result_packet(inner.message_id, inner.body)));
        }
        cv_.notify_all();
    }

    // Wraps a plaintext rpc_result body in the encrypted envelope the client expects.
    Bytes encrypt_reply(const InnerMessage& request, const Bytes& body) {
        InnerMessage reply;
        reply.salt = 0x1111222233334444ull;
        reply.session_id = request.session_id;
        reply.message_id = make_message_id() + 4;
        reply.seq_no = 1;
        reply.body = body;
        return encrypt_packet(key_, serialize_inner(reply), false);
    }

    Bytes receive_packet() override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, receive_timeout_, [this] {
                return !replies_.empty();
            })) {
            throw ConnectionTimeout("receive timed out");
        }
        Bytes out = std::move(replies_.front());
        replies_.pop_front();
        return out;
    }

    // Same contract as Connection: returns when a reply exists, throws OperationCancelled when stop is requested.
    Bytes receive_packet_cancellable(const std::stop_token& stop,
                                     std::chrono::steady_clock::time_point request_deadline) override {
        const auto deadline = std::min(request_deadline, std::chrono::steady_clock::now() + receive_timeout_);
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            if (!replies_.empty()) {
                Bytes out = std::move(replies_.front());
                replies_.pop_front();
                return out;
            }
            if (stop.stop_requested())
                throw OperationCancelled();
            if (std::chrono::steady_clock::now() >= deadline)
                throw ConnectionTimeout("receive timed out");
            cv_.wait_for(lock, 2ms);
        }
    }

    std::chrono::milliseconds receive_timeout_ { 10000 };

private:
    Bytes key_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Bytes> replies_;
    std::vector<std::pair<std::uint64_t, Bytes>> stale_;
    bool echo_ = true;
    int sends_ = 0;
    std::uint64_t last_request_id_ = 0;
};

constexpr auto kNoDeadline = std::chrono::steady_clock::time_point::max();

SessionData session_for(const Bytes& key) {
    return SessionData { 2, key, 0x1111222233334444ull, 0 };
}

TEST(InFlightCancel, CancelBeforeDispatchSendsNothing) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    MtSession session(server, session_for(key));
    std::stop_source source;
    source.request_stop();
    session.set_cancel_token(source.get_token());
    EXPECT_THROW(session.invoke(Bytes(8, 1)), OperationCancelled);
    EXPECT_EQ(server.sends(), 0) << "a request cancelled before dispatch must not reach the wire";
}

TEST(InFlightCancel, CancelWhileWaitingReturnsPromptly) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    MtSession session(server, session_for(key));
    std::stop_source source;
    session.set_cancel_token(source.get_token());

    std::thread canceller([&source] {
        std::this_thread::sleep_for(100ms);
        source.request_stop();
    });
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_THROW(session.invoke(Bytes(8, 2)), OperationCancelled);
    canceller.join();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
    EXPECT_EQ(server.sends(), 1) << "the request was sent before the cancel, so it is in flight";
}

TEST(InFlightCancel, CancelAfterResponseKeepsTheResult) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    MtSession session(server, session_for(key));
    std::stop_source source;
    session.set_cancel_token(source.get_token());
    const Bytes body(8, 3);
    EXPECT_EQ(session.invoke(body), body);
    source.request_stop(); // too late: the answer is already delivered
    session.set_cancel_token(std::stop_token {});
    EXPECT_EQ(session.invoke(Bytes(8, 4)), Bytes(8, 4)) << "the session stays usable after a late cancel";
}

TEST(InFlightCancel, TimeoutWinsWhenNoCancelArrives) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    server.receive_timeout_ = std::chrono::milliseconds(5000);
    MtSession session(server, session_for(key));
    session.set_request_timeout(200ms);
    std::stop_source source; // never requested
    session.set_cancel_token(source.get_token());
    EXPECT_THROW(session.invoke(Bytes(8, 5)), ConnectionTimeout);
}

TEST(InFlightCancel, CancelWinsWhenItArrivesBeforeTheTimeout) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    MtSession session(server, session_for(key));
    session.set_request_timeout(10s);
    std::stop_source source;
    session.set_cancel_token(source.get_token());
    std::thread canceller([&source] {
        std::this_thread::sleep_for(100ms);
        source.request_stop();
    });
    EXPECT_THROW(session.invoke(Bytes(8, 6)), OperationCancelled);
    canceller.join();
}

TEST(InFlightCancel, LateReplyToACancelledRequestIsIgnored) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    MtSession session(server, session_for(key));
    std::stop_source source;
    session.set_cancel_token(source.get_token());
    std::thread canceller([&source] {
        std::this_thread::sleep_for(50ms);
        source.request_stop();
    });
    EXPECT_THROW(session.invoke(Bytes(8, 7)), OperationCancelled);
    canceller.join();
    const std::uint64_t cancelled_id = server.last_request_id();

    // The server now delivers the cancelled request's answer ahead of the next request's answer. The session must
    // skip it and return only the new result.
    server.queue_stale_reply(cancelled_id, Bytes(8, 0xEE));
    server.set_echo(true);
    session.set_cancel_token(std::stop_token {});
    const Bytes fresh(8, 0x5A);
    EXPECT_EQ(session.invoke(fresh), fresh);
}

TEST(InFlightCancel, ConcurrentCancellationsAreSafe) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    MtSession session(server, session_for(key));
    std::stop_source source;
    session.set_cancel_token(source.get_token());
    std::atomic<int> ready { 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&source, &ready] {
            ready.fetch_add(1);
            while (ready.load() < 4)
                std::this_thread::yield();
            source.request_stop();
        });
    }
    EXPECT_THROW(session.invoke(Bytes(8, 8)), OperationCancelled);
    for (auto& t : threads)
        t.join();
    EXPECT_TRUE(source.stop_requested());

    // A fresh token after the storm: the session works.
    server.set_echo(true);
    session.set_cancel_token(std::stop_token {});
    EXPECT_EQ(session.invoke(Bytes(8, 9)), Bytes(8, 9));
}

TEST(InFlightCancel, RunnerShutdownStopsAnInFlightRequest) {
    const Bytes key = random_bytes(256);
    ScriptedServer server(key);
    server.set_echo(false);
    server.receive_timeout_ = std::chrono::milliseconds(20000);
    MtSession session(server, session_for(key));
    auto runner = std::make_unique<cppgram::async::Runner>();
    auto result = runner->post([&session](std::stop_token token) {
        session.set_cancel_token(token);
        return session.invoke(Bytes(8, 10));
    });
    while (server.sends() == 0)
        std::this_thread::sleep_for(1ms); // the request is on the wire

    const auto begin = std::chrono::steady_clock::now();
    runner.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s) << "shutdown must not wait for the 20 s receive timeout";
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
}

// ---- real socket: Connection's cancellable receive against a silent loopback peer

struct LoopbackPeer {
    int listen_fd = -1;
    int client_fd = -1;
    std::uint16_t port = 0;

    LoopbackPeer() {
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_fd, 1) != 0) {
            throw std::runtime_error("loopback listen failed");
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
    }
    ~LoopbackPeer() {
        if (client_fd >= 0)
            ::close(client_fd);
        if (listen_fd >= 0)
            ::close(listen_fd);
    }
    void accept_one() {
        client_fd = ::accept(listen_fd, nullptr, nullptr);
    }
    // Intermediate transport frame: 4-byte little-endian length, then the payload.
    void send_frame(const Bytes& payload) {
        std::uint32_t len = static_cast<std::uint32_t>(payload.size());
        std::uint8_t header[4] = { static_cast<std::uint8_t>(len), static_cast<std::uint8_t>(len >> 8),
                                   static_cast<std::uint8_t>(len >> 16), static_cast<std::uint8_t>(len >> 24) };
        ::send(client_fd, header, 4, 0);
        ::send(client_fd, payload.data(), payload.size(), 0);
    }
};

TEST(InFlightCancelSocket, CancelStopsASilentWaitPromptly) {
    LoopbackPeer peer;
    std::thread acceptor([&peer] {
        peer.accept_one();
    });
    Connection conn;
    conn.connect("127.0.0.1", peer.port);
    acceptor.join();
    conn.set_receive_timeout(0); // no receive timeout: only the stop token can end the wait

    std::stop_source source;
    std::thread canceller([&source] {
        std::this_thread::sleep_for(100ms);
        source.request_stop();
    });
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_THROW(conn.receive_packet_cancellable(source.get_token(), kNoDeadline), OperationCancelled);
    canceller.join();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
}

TEST(InFlightCancelSocket, BytesArrivingAfterCancelAreKeptForTheNextReceive) {
    LoopbackPeer peer;
    std::thread acceptor([&peer] {
        peer.accept_one();
    });
    Connection conn;
    conn.connect("127.0.0.1", peer.port);
    acceptor.join();
    conn.set_receive_timeout(0);

    std::stop_source source;
    source.request_stop();
    EXPECT_THROW(conn.receive_packet_cancellable(source.get_token(), kNoDeadline), OperationCancelled);

    const Bytes payload { 1, 2, 3, 4, 5, 6, 7, 8 };
    peer.send_frame(payload);
    EXPECT_EQ(conn.receive_packet(), payload) << "a cancelled wait must not consume or corrupt later frames";
}

TEST(InFlightCancelSocket, ReceiveTimeoutStillAppliesToTheCancellableWait) {
    LoopbackPeer peer;
    std::thread acceptor([&peer] {
        peer.accept_one();
    });
    Connection conn;
    conn.connect("127.0.0.1", peer.port);
    acceptor.join();
    conn.set_receive_timeout(150);

    std::stop_source source; // never requested
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_THROW(conn.receive_packet_cancellable(source.get_token(), kNoDeadline), ConnectionTimeout);
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 3s);
}

} // namespace
