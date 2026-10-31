#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <zlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

#include "cppgram/client.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"

using namespace cppgram::mtproto;
namespace ty = cppgram::mtproto::tl::types;

namespace {

// Server that decrypts client packets and answers with an rpc_result that echoes the request body.
// `salt_rounds` makes it reply with bad_server_salt first; `error_code` makes it answer with rpc_error.
class EncryptedEchoServer : public PacketChannel {
public:
    EncryptedEchoServer(Bytes key, int salt_rounds = 0, int error_code = 0)
        : key_(std::move(key)), salt_rounds_(salt_rounds), error_code_(error_code) {}

    void send_packet(const Bytes& packet) override {
        const Bytes plain = decrypt_packet(key_, packet, true);
        const InnerMessage inner = parse_inner(plain);
        last_salt_ = inner.salt;
        if ((inner.seq_no & 1u) == 0) {
            return;  // service messages (acks) get no reply
        }
        ++requests_;
        InnerMessage reply;
        reply.salt = server_salt_;
        reply.session_id = inner.session_id;
        reply.message_id = make_message_id() + 4;
        reply.seq_no = 1;

        if (salt_rounds_ > 0) {
            --salt_rounds_;
            ty::bad_server_salt bad;
            bad.bad_msg_id = static_cast<std::int64_t>(inner.message_id);
            bad.bad_msg_seqno = 1;
            bad.error_code = 48;
            bad.new_server_salt = 0x0102030405060708ll;
            TlWriter w;
            bad.serialize(w);
            reply.body = w.take();
            server_salt_ = 0x0102030405060708ull;
        } else if (error_code_ != 0) {
            ty::rpc_error err;
            err.error_code = error_code_;
            err.error_message = "PEER_ID_INVALID";
            TlWriter inner_w;
            err.serialize(inner_w);
            reply.body = rpc_result(inner.message_id, inner_w.bytes());
        } else {
            reply.body = rpc_result(inner.message_id, inner.body);
        }
        replies_.push_back(encrypt_packet(key_, serialize_inner(reply), false));
    }

    Bytes receive_packet() override {
        Bytes out = std::move(replies_.front());
        replies_.pop_front();
        return out;
    }

    int requests() const { return requests_; }
    std::uint64_t last_salt() const { return last_salt_; }

private:
    static Bytes rpc_result(std::uint64_t req_msg_id, const Bytes& result) {
        TlWriter w;
        w.write_uint32(ty::rpc_result::kId);
        w.write_uint64(req_msg_id);
        w.write_raw(result);
        return w.take();
    }

    Bytes key_;
    int salt_rounds_;
    int error_code_;
    std::uint64_t server_salt_ = 0x1111222233334444ull;
    std::uint64_t last_salt_ = 0;
    int requests_ = 0;
    std::deque<Bytes> replies_;
};

Bytes gzip_compress(const Bytes& data) {
    z_stream s{};
    deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    s.next_in = const_cast<Bytef*>(data.data());
    s.avail_in = static_cast<uInt>(data.size());
    Bytes out(data.size() + 256);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

}  // namespace

TEST(Session, InvokeRoundTripsThroughEncryptedChannel) {
    const Bytes key = random_bytes(256);
    EncryptedEchoServer server(key);
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    const Bytes body = Bytes{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    EXPECT_EQ(session.invoke(body), body);
    EXPECT_EQ(server.requests(), 1);
}

TEST(Session, RetriesAfterBadServerSalt) {
    const Bytes key = random_bytes(256);
    EncryptedEchoServer server(key, 1);
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    const Bytes body = Bytes(8, 0x42);
    EXPECT_EQ(session.invoke(body), body);
    EXPECT_EQ(server.requests(), 2);
    EXPECT_EQ(session.data().server_salt, 0x0102030405060708ull);
}

TEST(Session, RpcErrorIsRaised) {
    const Bytes key = random_bytes(256);
    EncryptedEchoServer server(key, 0, 400);
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    try {
        session.invoke(Bytes(8, 0));
        FAIL() << "expected RpcError";
    } catch (const RpcError& e) {
        EXPECT_EQ(e.code(), 400);
    }
}

TEST(Session, GunzipUnpacksGzipData) {
    const std::string text = "cppgram gzip payload";
    const Bytes payload(text.begin(), text.end());
    EXPECT_EQ(gunzip(gzip_compress(payload)), payload);
}

TEST(Session, SessionFileRoundTrip) {
    SessionData data;
    data.dc_id = 4;
    data.auth_key = random_bytes(256);
    data.server_salt = 0xdeadbeefcafef00dull;
    data.time_offset = -7;
    const std::string path = ::testing::TempDir() + "cppgram_session_test.bin";
    save_session_file(path, data);
    SessionData loaded;
    ASSERT_TRUE(load_session_file(path, loaded));
    EXPECT_EQ(loaded.dc_id, 4);
    EXPECT_EQ(loaded.auth_key, data.auth_key);
    EXPECT_EQ(loaded.server_salt, data.server_salt);
    EXPECT_EQ(loaded.time_offset, -7);
}

TEST(Connection, FramesOverLoopbackTcp) {
    // A plain TCP echo peer on 127.0.0.1 that speaks the intermediate transport.
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);
    socklen_t len = sizeof(addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    const std::uint16_t port = ntohs(addr.sin_port);

    std::thread peer([listen_fd] {
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        Transport transport(TransportKind::Intermediate);
        std::uint8_t buf[1024];
        std::uint8_t init[4];
        if (::recv(fd, init, 4, MSG_WAITALL) != 4 || init[0] != 0xee) {
            ::close(fd);
            return;
        }
        while (true) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            transport.feed(Bytes(buf, buf + n));
            if (auto packet = transport.next()) {
                const Bytes frame = transport.encode(*packet);
                ::send(fd, frame.data(), frame.size(), 0);
                break;
            }
        }
        ::close(fd);
    });

    Connection conn(TransportKind::Intermediate);
    conn.connect("127.0.0.1", port);
    const Bytes payload = Bytes(16, 0x5a);
    conn.send_packet(payload);
    EXPECT_EQ(conn.receive_packet(), payload);
    conn.close();
    peer.join();
    ::close(listen_fd);
}

TEST(Client, RequiresConnectBeforeCalls) {
    cppgram::ClientOptions options;
    options.api_id = 1;
    options.api_hash = "hash";
    cppgram::Client client(options);
    EXPECT_FALSE(client.is_connected());
    EXPECT_THROW(client.login_bot("123:token"), std::runtime_error);
    EXPECT_THROW(client.send_message(std::string(""), "hi"), std::runtime_error);
    EXPECT_THROW(client.get_me(), std::runtime_error);
}

#include <sys/stat.h>
#include <fcntl.h>
#include <set>

#include "../../src/secure_file.h"
#include "../../src/secure_random.h"

TEST(SecurityHardening, SecureRandomIdsAreDistinct) {
    std::set<std::int64_t> seen;
    for (int i = 0; i < 256; ++i) seen.insert(cppgram::detail::secure_random_i64());
    EXPECT_EQ(seen.size(), 256u);
}

TEST(SecurityHardening, PrivateFileIsOwnerOnly) {
    const std::string path = "/tmp/cppgram_perm_test_" + std::to_string(::getpid());
    ::unlink(path.c_str());
    cppgram::detail::ensure_private_file(path);
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ::chmod(path.c_str(), 0644);  // existing file with loose mode must be tightened
    cppgram::detail::ensure_private_file(path);
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ::unlink(path.c_str());
}

TEST(SecurityHardening, ConstantTimeEqualMatchesNormalCompare) {
    using cppgram::mtproto::Bytes;
    using cppgram::mtproto::constant_time_equal;
    EXPECT_TRUE(constant_time_equal(Bytes{1, 2, 3}, Bytes{1, 2, 3}));
    EXPECT_FALSE(constant_time_equal(Bytes{1, 2, 3}, Bytes{1, 2, 4}));
    EXPECT_FALSE(constant_time_equal(Bytes{1, 2}, Bytes{1, 2, 3}));
    EXPECT_TRUE(constant_time_equal(Bytes{}, Bytes{}));
}

namespace {

// Answers every request with unrelated packets forever, never with an rpc_result.
class ChattySilentServer : public PacketChannel {
public:
    explicit ChattySilentServer(Bytes key) : key_(std::move(key)) {}
    void send_packet(const Bytes&) override {}
    Bytes receive_packet() override {
        InnerMessage msg;
        msg.salt = 1;
        msg.session_id = 2;
        msg.message_id = make_message_id() + 4;
        msg.seq_no = 0;  // no ack needed, keeps the test focused on the deadline
        msg.body = Bytes{1, 2, 3, 4, 5, 6, 7, 8};  // unknown constructor, ignored by dispatch
        return encrypt_packet(key_, serialize_inner(msg), false);
    }

private:
    Bytes key_;
};

}  // namespace

TEST(Session, InvokeHonoursRequestTimeoutWhenServerKeepsTalking) {
    const Bytes key = random_bytes(256);
    ChattySilentServer server(key);
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    session.set_request_timeout(std::chrono::milliseconds(150));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(session.invoke(Bytes{0x10, 0x20, 0x30, 0x40}), ConnectionTimeout);
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_LT(took, std::chrono::seconds(5));
}

TEST(SessionStorage, AtomicSaveLeavesNoTempFileAndRoundTrips) {
    const std::string path = "/tmp/cppgram_atomic_test_" + std::to_string(::getpid()) + ".session";
    const std::string tmp = path + ".tmp";
    ::unlink(path.c_str());
    SessionData first{2, random_bytes(256), 0x1111111111111111ull, -5};
    SessionData second{4, random_bytes(256), 0x2222222222222222ull, 7};
    save_session_file(path, first);
    save_session_file(path, second);  // replace an existing session
    SessionData loaded;
    ASSERT_TRUE(load_session_file(path, loaded));
    EXPECT_EQ(loaded.dc_id, 4);
    EXPECT_EQ(loaded.server_salt, 0x2222222222222222ull);
    EXPECT_EQ(loaded.auth_key, second.auth_key);
    struct stat st{};
    EXPECT_NE(::stat(tmp.c_str(), &st), 0) << "temp file must not survive a successful save";
    ::unlink(path.c_str());
}

namespace {

// Server whose clock is one hour ahead. The first request gets a bad_msg_notification (code 16, msg_id too low)
// carrying the server's time; the client must correct its offset and resend with a msg_id near server time.
class TimeSkewServer : public PacketChannel {
public:
    TimeSkewServer(Bytes key, std::int64_t skew_seconds) : key_(std::move(key)), skew_(skew_seconds) {}

    void send_packet(const Bytes& packet) override {
        const InnerMessage inner = parse_inner(decrypt_packet(key_, packet, true));
        if ((inner.seq_no & 1u) == 0) return;  // acks get no reply
        ++requests_;
        const std::int64_t server_now = unix_now() + skew_;
        InnerMessage reply;
        reply.salt = 1;
        reply.session_id = inner.session_id;
        reply.seq_no = 1;
        reply.message_id = (static_cast<std::uint64_t>(server_now) << 32) | 4u;
        if (requests_ == 1) {
            first_msg_id_ = inner.message_id;
            ty::bad_msg_notification bad;
            bad.bad_msg_id = static_cast<std::int64_t>(inner.message_id);
            bad.bad_msg_seqno = 1;
            bad.error_code = 16;
            TlWriter w;
            bad.serialize(w);
            reply.body = w.take();
        } else {
            corrected_msg_id_ = inner.message_id;
            TlWriter w;
            w.write_uint32(ty::rpc_result::kId);
            w.write_uint64(inner.message_id);
            w.write_raw(inner.body);
            reply.body = w.take();
        }
        replies_.push_back(encrypt_packet(key_, serialize_inner(reply), false));
    }

    Bytes receive_packet() override {
        Bytes out = std::move(replies_.front());
        replies_.pop_front();
        return out;
    }

    int requests() const { return requests_; }
    std::uint64_t first_msg_id() const { return first_msg_id_; }
    std::uint64_t corrected_msg_id() const { return corrected_msg_id_; }

private:
    static std::int64_t unix_now() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    Bytes key_;
    std::int64_t skew_;
    int requests_ = 0;
    std::uint64_t first_msg_id_ = 0;
    std::uint64_t corrected_msg_id_ = 0;
    std::deque<Bytes> replies_;
};

}  // namespace

TEST(Session, BadMsgCode16CorrectsTimeOffsetAndResends) {
    const Bytes key = random_bytes(256);
    const std::int64_t skew = 3600;
    TimeSkewServer server(key, skew);
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    const Bytes body{0x10, 0x20, 0x30, 0x40};
    EXPECT_EQ(session.invoke(body), body);
    EXPECT_EQ(server.requests(), 2);
    // Offset learned from the server, within a few seconds of the real skew.
    EXPECT_NEAR(static_cast<double>(session.data().time_offset), static_cast<double>(skew), 5.0);
    // The resent message id must be near server time (high 32 bits), not the old local time.
    const auto corrected_secs = static_cast<std::int64_t>(server.corrected_msg_id() >> 32);
    const auto now_secs = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch()).count();
    EXPECT_NEAR(static_cast<double>(corrected_secs), static_cast<double>(now_secs + skew), 5.0);
}

namespace {

// Answers the first request with an rpc_error carrying `flood_message` and the rest with an echo.
class FloodServer : public PacketChannel {
public:
    FloodServer(Bytes key, std::string flood_message) : key_(std::move(key)), flood_(std::move(flood_message)) {}

    void send_packet(const Bytes& packet) override {
        const InnerMessage inner = parse_inner(decrypt_packet(key_, packet, true));
        if ((inner.seq_no & 1u) == 0) return;
        ++requests_;
        InnerMessage reply;
        reply.salt = 1;
        reply.session_id = inner.session_id;
        reply.seq_no = 1;
        reply.message_id = make_message_id() + 4;
        TlWriter result;
        if (requests_ == 1) {
            ty::rpc_error err;
            err.error_code = 420;
            err.error_message = flood_;
            TlWriter e;
            err.serialize(e);
            result.write_uint32(ty::rpc_result::kId);
            result.write_uint64(inner.message_id);
            result.write_raw(e.bytes());
        } else {
            result.write_uint32(ty::rpc_result::kId);
            result.write_uint64(inner.message_id);
            result.write_raw(inner.body);
        }
        reply.body = result.take();
        replies_.push_back(encrypt_packet(key_, serialize_inner(reply), false));
    }

    Bytes receive_packet() override {
        Bytes out = std::move(replies_.front());
        replies_.pop_front();
        return out;
    }

    int requests() const { return requests_; }

private:
    Bytes key_;
    std::string flood_;
    int requests_ = 0;
    std::deque<Bytes> replies_;
};

}  // namespace

TEST(Session, FloodWaitWithinLimitSleepsAndResends) {
    const Bytes key = random_bytes(256);
    FloodServer server(key, "FLOOD_WAIT_1");
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    RetryPolicy policy;
    policy.max_flood_wait = std::chrono::seconds(5);
    session.set_retry_policy(policy);
    const Bytes body{0x01, 0x02, 0x03, 0x04};
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(session.invoke(body), body);
    EXPECT_EQ(server.requests(), 2);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
}

TEST(Session, FloodWaitBeyondLimitIsReturnedWithoutSleeping) {
    const Bytes key = random_bytes(256);
    FloodServer server(key, "FLOOD_WAIT_100");
    MtSession session(server, SessionData{2, key, 0x1111222233334444ull, 0});
    RetryPolicy policy;
    policy.max_flood_wait = std::chrono::seconds(5);
    session.set_retry_policy(policy);
    EXPECT_THROW(session.invoke(Bytes{0x01, 0x02, 0x03, 0x04}), FloodWaitError);
    EXPECT_EQ(server.requests(), 1);
}

TEST(ClientRetry, ConnectRetriesBoundedThenFailsFast) {
    using cppgram::ClientOptions;
    using cppgram::Client;
    ClientOptions opts;
    opts.api_id = 1;
    opts.api_hash = "x";
    opts.dc_ip = "127.0.0.1";
    opts.dc_port = 1;  // nothing listens here: connect is refused
    opts.session_path = "";
    opts.retry.max_attempts = 3;
    opts.retry.base = std::chrono::milliseconds(10);
    opts.retry.cap = std::chrono::milliseconds(20);
    Client client(opts);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.connect(), ConnectionError);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
}
