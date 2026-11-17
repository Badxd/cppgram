// Connection behaviour over real loopback sockets with scripted peers: disconnects, reconnects, timeouts,
// refused connections, and the obfuscated proxy path end to end. No external network access is needed.
// The obfuscated peer is written from the same layout rules as obfuscated.h, so it checks the wiring and
// framing, not compatibility with Telegram's proxy servers.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/proxy_secret.h"

using namespace cppgram::mtproto;

namespace {

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int get() const { return fd_; }

private:
    int fd_;
};

// Listening loopback socket on an ephemeral port.
Fd listen_loopback(std::uint16_t& port) {
    Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
    int one = 1;
    ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd.get(), 4) != 0) {
        throw std::runtime_error("test listener setup failed");
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &len);
    port = ntohs(addr.sin_port);
    return fd;
}

// Port with nothing listening: bind, read the port, close.
std::uint16_t closed_loopback_port() {
    std::uint16_t port = 0;
    Fd fd = listen_loopback(port);
    return port;
}

void read_exact(int fd, std::uint8_t* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r <= 0) throw std::runtime_error("scripted peer: client went away");
        got += static_cast<std::size_t>(r);
    }
}

Bytes read_bytes(int fd, std::size_t n) {
    Bytes out(n);
    read_exact(fd, out.data(), n);
    return out;
}

void send_bytes(int fd, const Bytes& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) throw std::runtime_error("scripted peer: send failed");
        sent += static_cast<std::size_t>(n);
    }
}

const char* kSecretHex = "11223344556677889900aabbccddeeff";

std::array<std::uint8_t, 16> secret_bytes() {
    return parse_proxy_secret(kSecretHex).key;
}

}  // namespace

TEST(ConnectionNet, DirectConnectToClosedPortIsRefusedNotTimedOut) {
    const std::uint16_t port = closed_loopback_port();
    Connection conn;
    EXPECT_THROW(conn.connect("127.0.0.1", port), ConnectionError);
    EXPECT_FALSE(conn.is_open());
}

TEST(ConnectionNet, DirectConnectSucceedsAndCanReconnectAfterDrop) {
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    Connection conn(TransportKind::Intermediate);
    conn.set_connect_timeout(2000);
    conn.connect("127.0.0.1", port);
    EXPECT_TRUE(conn.is_open());

    // Peer accepts and drops the connection without replying: the client must see a closed connection.
    std::thread peer([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        std::uint8_t tmp[64];
        ::recv(c, tmp, sizeof(tmp), 0);
        ::close(c);
    });
    conn.send_packet(Bytes{1, 2, 3, 4});
    conn.set_receive_timeout(3000);
    EXPECT_THROW(conn.receive_packet(), ConnectionError);
    peer.join();
    conn.close();
    EXPECT_FALSE(conn.is_open());

    // Reconnect on the same Connection object to a live peer that answers.
    std::thread second([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        std::uint8_t tmp[256];
        ::recv(c, tmp, sizeof(tmp), 0);
        // Intermediate framing: length word then payload.
        const Bytes reply{4, 0, 0, 0, 'o', 'k', '!', '!'};
        send_bytes(c, reply);
        ::close(c);
    });
    conn.connect("127.0.0.1", port);
    conn.set_receive_timeout(3000);
    conn.send_packet(Bytes{9, 9, 9, 9});
    EXPECT_EQ(conn.receive_packet(), (Bytes{'o', 'k', '!', '!'}));
    second.join();
}

TEST(ConnectionNet, SilentPeerTriggersReceiveTimeout) {
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    std::thread peer([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ::close(c);
    });
    Connection conn;
    conn.connect("127.0.0.1", port);
    conn.set_receive_timeout(200);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(conn.receive_packet(), ConnectionTimeout);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(1200));
    peer.join();
}

TEST(ConnectionNet, ConnectTimeoutIsEnforcedWhenAcceptQueueIsFull) {
    // A listener with a zero-length accept queue, already filled by two clients, drops further SYNs on Linux,
    // so a new connect stays in progress. The connect must give up at the configured timeout.
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    ::listen(listener.get(), 0);
    // Fillers connect without blocking: once the queue is full a blocking connect would hang for minutes.
    Fd filler1(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0));
    Fd filler2(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    ::connect(filler1.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::connect(filler2.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Connection conn;
    conn.set_connect_timeout(300);
    const auto start = std::chrono::steady_clock::now();
    bool timed_out = false;
    try {
        conn.connect("127.0.0.1", port);
    } catch (const ConnectionTimeout&) {
        timed_out = true;
    } catch (const ConnectionError&) {
        // Some kernels refuse instead of dropping the SYN. Still a bounded failure, checked below.
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (!timed_out && conn.is_open()) {
        GTEST_SKIP() << "kernel completed the connection despite a full accept queue";
    }
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_FALSE(conn.is_open());
}

TEST(ConnectionNet, InvalidIpv4LiteralIsRejectedWithoutConnecting) {
    Connection conn;
    EXPECT_THROW(conn.connect("not-an-ip", 443), ConnectionError);
}

TEST(ObfuscatedNet, ConnectionToClosedProxyIsTransientProxyError) {
    const std::uint16_t port = closed_loopback_port();
    Connection conn;
    try {
        conn.connect_obfuscated("127.0.0.1", port, parse_proxy_secret(kSecretHex), 2,
                                std::chrono::milliseconds(1000));
        FAIL() << "expected ProxyError";
    } catch (const ProxyError& e) {
        EXPECT_TRUE(e.transient());
    }
}

TEST(ObfuscatedNet, PlainSecretRoundTripThroughScriptedProxy) {
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    const auto key = secret_bytes();

    std::thread proxy([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        const Bytes handshake = read_bytes(c, kObfuscatedInitSize);
        auto view = parse_obfuscated_handshake(handshake, key);
        EXPECT_EQ(view.framing, ObfuscatedFraming::Intermediate);
        EXPECT_EQ(view.dc_id, 5);

        const Bytes len_wire = read_bytes(c, 4);
        const Bytes len_plain = view.from_client.apply(len_wire);
        const std::uint32_t len = len_plain[0] | (len_plain[1] << 8) | (len_plain[2] << 16) |
                                  (static_cast<std::uint32_t>(len_plain[3]) << 24);
        const Bytes payload = view.from_client.apply(read_bytes(c, len));
        EXPECT_EQ(payload, (Bytes{'p', 'i', 'n', 'g'}));

        const Bytes reply_frame{4, 0, 0, 0, 'p', 'o', 'n', 'g'};
        send_bytes(c, view.to_client.apply(reply_frame));
        ::close(c);
    });

    Connection conn;
    conn.set_connect_timeout(2000);
    conn.connect_obfuscated("127.0.0.1", port, parse_proxy_secret(kSecretHex), 5,
                            std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    conn.send_packet(Bytes{'p', 'i', 'n', 'g'});
    EXPECT_EQ(conn.receive_packet(), (Bytes{'p', 'o', 'n', 'g'}));
    proxy.join();
}

TEST(ObfuscatedNet, PaddedSecretFramingIsAcceptedByScriptedProxy) {
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    const auto key = secret_bytes();

    std::thread proxy([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        auto view = parse_obfuscated_handshake(read_bytes(c, kObfuscatedInitSize), key);
        EXPECT_EQ(view.framing, ObfuscatedFraming::PaddedIntermediate);
        const Bytes len_plain = view.from_client.apply(read_bytes(c, 4));
        const std::uint32_t len = len_plain[0] | (len_plain[1] << 8) | (len_plain[2] << 16) |
                                  (static_cast<std::uint32_t>(len_plain[3]) << 24);
        const Bytes body = view.from_client.apply(read_bytes(c, len));
        // The body is the payload followed by 0..15 bytes of padding.
        EXPECT_GE(len, 8u);
        EXPECT_LE(len, 8u + 15u);
        EXPECT_EQ(Bytes(body.begin(), body.begin() + 8), (Bytes{1, 2, 3, 4, 5, 6, 7, 8}));
        ::close(c);
    });

    Connection conn;
    conn.connect_obfuscated("127.0.0.1", port, parse_proxy_secret("dd" + std::string(kSecretHex)), 2,
                            std::chrono::milliseconds(2000));
    conn.send_packet(Bytes{1, 2, 3, 4, 5, 6, 7, 8});
    proxy.join();
}

TEST(ObfuscatedNet, ProxyThatDropsAfterHandshakeIsDetected) {
    std::uint16_t port = 0;
    Fd listener = listen_loopback(port);
    std::thread proxy([&] {
        const int c = ::accept(listener.get(), nullptr, nullptr);
        read_bytes(c, kObfuscatedInitSize);
        ::close(c);
    });
    Connection conn;
    conn.connect_obfuscated("127.0.0.1", port, parse_proxy_secret(kSecretHex), 2,
                            std::chrono::milliseconds(2000));
    conn.set_receive_timeout(3000);
    proxy.join();
    try {
        conn.receive_packet();
        FAIL() << "expected the dropped proxy connection to fail";
    } catch (const ConnectionTimeout&) {
        FAIL() << "a closed connection must not look like a timeout";
    } catch (const ConnectionError& e) {
        EXPECT_NE(std::string(e.what()).find("closed"), std::string::npos);
    }
}

TEST(ObfuscatedNet, FakeTlsSecretIsRejectedBeforeConnecting) {
    Connection conn;
    const std::string hex = "ee" + std::string(kSecretHex) + "6578616d706c652e636f6d";
    EXPECT_THROW(conn.connect_obfuscated("127.0.0.1", 9, parse_proxy_secret(hex), 2,
                                         std::chrono::milliseconds(200)),
                 std::invalid_argument);
}
