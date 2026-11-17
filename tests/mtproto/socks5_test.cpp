// SOCKS5 client against a scripted local proxy (loopback only, no network access needed).
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/mtproto/socks5.h"
#include "cppgram/mtproto/connection.h"

using namespace cppgram::mtproto;

namespace {

void read_exact(int fd, std::uint8_t* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r <= 0) throw std::runtime_error("fake proxy: client went away");
        got += static_cast<std::size_t>(r);
    }
}

void write_all(int fd, const std::vector<std::uint8_t>& v) {
    ::send(fd, v.data(), v.size(), MSG_NOSIGNAL);
}

// Scripted SOCKS5 server. Serves one connection, then exits.
class FakeProxy {
public:
    enum class Mode { Ok, DomainBound, RequireAuth, RejectAuth, NoMethod, RefuseTarget, Hang };

    explicit FakeProxy(Mode mode) : mode_(mode) {
        lfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(lfd_, 4);
        socklen_t len = sizeof addr;
        ::getsockname(lfd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeProxy() {
        if (thread_.joinable()) thread_.join();
        ::close(lfd_);
    }
    std::uint16_t port() const { return port_; }
    void join() { if (thread_.joinable()) thread_.join(); }

    std::uint8_t atyp = 0;
    std::string target;
    std::uint16_t target_port = 0;
    std::string user, pass;

private:
    void serve() {
        const int fd = ::accept(lfd_, nullptr, nullptr);
        if (fd < 0) return;
        std::uint8_t h[2];
        read_exact(fd, h, 2);
        std::vector<std::uint8_t> methods(h[1]);
        read_exact(fd, methods.data(), methods.size());
        auto offers = [&](std::uint8_t m) {
            for (auto x : methods) if (x == m) return true;
            return false;
        };
        if (mode_ == Mode::NoMethod) {
            write_all(fd, {5, 0xFF});
            ::close(fd);
            return;
        }
        const bool want_auth = mode_ == Mode::RequireAuth || mode_ == Mode::RejectAuth;
        if (want_auth && !offers(0x02)) {
            write_all(fd, {5, 0xFF});
            ::close(fd);
            return;
        }
        write_all(fd, {5, static_cast<std::uint8_t>(want_auth ? 0x02 : 0x00)});
        if (want_auth) {
            std::uint8_t v[2];
            read_exact(fd, v, 2);
            user.resize(v[1]);
            read_exact(fd, reinterpret_cast<std::uint8_t*>(user.data()), v[1]);
            std::uint8_t pl = 0;
            read_exact(fd, &pl, 1);
            pass.resize(pl);
            read_exact(fd, reinterpret_cast<std::uint8_t*>(pass.data()), pl);
            const bool reject = mode_ == Mode::RejectAuth;
            write_all(fd, {1, static_cast<std::uint8_t>(reject ? 1 : 0)});
            if (reject) {
                ::close(fd);
                return;
            }
        }
        std::uint8_t req[4];
        read_exact(fd, req, 4);
        atyp = req[3];
        if (atyp == 1) {
            std::uint8_t a[4];
            read_exact(fd, a, 4);
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, a, buf, sizeof buf);
            target = buf;
        } else if (atyp == 4) {
            std::uint8_t a[16];
            read_exact(fd, a, 16);
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, a, buf, sizeof buf);
            target = buf;
        } else {
            std::uint8_t len = 0;
            read_exact(fd, &len, 1);
            target.resize(len);
            read_exact(fd, reinterpret_cast<std::uint8_t*>(target.data()), len);
        }
        std::uint8_t p[2];
        read_exact(fd, p, 2);
        target_port = static_cast<std::uint16_t>((p[0] << 8) | p[1]);

        if (mode_ == Mode::RefuseTarget) {
            write_all(fd, {5, 5, 0, 1, 0, 0, 0, 0, 0, 0});
        } else if (mode_ == Mode::Hang) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        } else if (mode_ == Mode::DomainBound) {
            std::vector<std::uint8_t> r{5, 0, 0, 3, 8};
            const std::string bound = "bnd.test";
            r.insert(r.end(), bound.begin(), bound.end());
            r.insert(r.end(), {0, 80});
            write_all(fd, r);
        } else {
            write_all(fd, {5, 0, 0, 1, 0, 0, 0, 0, 0, 0});
        }
        ::close(fd);
    }

    Mode mode_;
    int lfd_ = -1;
    std::uint16_t port_ = 0;
    std::thread thread_;
};

Socks5Options opts_for(const FakeProxy& p) {
    Socks5Options o;
    o.proxy_host = "127.0.0.1";
    o.proxy_port = p.port();
    o.timeout = std::chrono::milliseconds(2000);
    return o;
}

}  // namespace

TEST(Socks5, Ipv4TargetIsSentAsBinaryAddress) {
    FakeProxy proxy(FakeProxy::Mode::Ok);
    const int fd = socks5_connect(opts_for(proxy), "203.0.113.7", 443);
    ::close(fd);
    proxy.join();
    EXPECT_EQ(proxy.atyp, 1);
    EXPECT_EQ(proxy.target, "203.0.113.7");
    EXPECT_EQ(proxy.target_port, 443);
}

TEST(Socks5, DomainTargetIsResolvedByProxy) {
    FakeProxy proxy(FakeProxy::Mode::Ok);
    ::close(socks5_connect(opts_for(proxy), "dc2.example.test", 443));
    proxy.join();
    EXPECT_EQ(proxy.atyp, 3);
    EXPECT_EQ(proxy.target, "dc2.example.test");
}

TEST(Socks5, Ipv6TargetIsSentAsBinaryAddress) {
    FakeProxy proxy(FakeProxy::Mode::Ok);
    ::close(socks5_connect(opts_for(proxy), "2001:db8::1", 443));
    proxy.join();
    EXPECT_EQ(proxy.atyp, 4);
    EXPECT_EQ(proxy.target, "2001:db8::1");
}

TEST(Socks5, DomainBoundAddressInReplyIsAccepted) {
    FakeProxy proxy(FakeProxy::Mode::DomainBound);
    ::close(socks5_connect(opts_for(proxy), "203.0.113.9", 80));
    proxy.join();
    EXPECT_EQ(proxy.target, "203.0.113.9");
}

TEST(Socks5, UsernamePasswordAuthIsSent) {
    FakeProxy proxy(FakeProxy::Mode::RequireAuth);
    Socks5Options o = opts_for(proxy);
    o.auth = Socks5Auth{"alice", "s3cret"};
    ::close(socks5_connect(o, "203.0.113.7", 443));
    proxy.join();
    EXPECT_EQ(proxy.user, "alice");
    EXPECT_EQ(proxy.pass, "s3cret");
}

TEST(Socks5, RejectedCredentialsFailAsNonTransient) {
    FakeProxy proxy(FakeProxy::Mode::RejectAuth);
    Socks5Options o = opts_for(proxy);
    o.auth = Socks5Auth{"alice", "wrong"};
    try {
        socks5_connect(o, "203.0.113.7", 443);
        FAIL() << "expected rejection";
    } catch (const Socks5Error& e) {
        EXPECT_FALSE(e.transient());
    }
}

TEST(Socks5, ProxyRequiringAuthWithoutCredentialsFails) {
    FakeProxy proxy(FakeProxy::Mode::RequireAuth);
    EXPECT_THROW(socks5_connect(opts_for(proxy), "203.0.113.7", 443), Socks5Error);
}

TEST(Socks5, NoAcceptableMethodFails) {
    FakeProxy proxy(FakeProxy::Mode::NoMethod);
    EXPECT_THROW(socks5_connect(opts_for(proxy), "203.0.113.7", 443), Socks5Error);
}

TEST(Socks5, RefusedTargetReportsReplyCodeAndIsNotRetried) {
    FakeProxy proxy(FakeProxy::Mode::RefuseTarget);
    try {
        socks5_connect(opts_for(proxy), "203.0.113.7", 443);
        FAIL() << "expected refusal";
    } catch (const Socks5Error& e) {
        EXPECT_EQ(e.reply_code(), 5);
        EXPECT_FALSE(e.transient());
    }
}

TEST(Socks5, SilentProxyTimesOutAsTransient) {
    FakeProxy proxy(FakeProxy::Mode::Hang);
    Socks5Options o = opts_for(proxy);
    o.timeout = std::chrono::milliseconds(300);
    const auto start = std::chrono::steady_clock::now();
    try {
        socks5_connect(o, "203.0.113.7", 443);
        FAIL() << "expected timeout";
    } catch (const Socks5Error& e) {
        EXPECT_TRUE(e.transient());
    }
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_LT(took, std::chrono::milliseconds(1400));
    proxy.join();
}

TEST(Socks5, InvalidInputsAreRejectedBeforeAnyNetworkUse) {
    Socks5Options o;
    o.proxy_host = "127.0.0.1";
    o.proxy_port = 1;  // nothing listens here; the error must come from validation, not from connecting
    EXPECT_THROW(socks5_connect(o, "", 443), std::invalid_argument);
    EXPECT_THROW(socks5_connect(o, std::string(256, 'a'), 443), std::invalid_argument);
    o.auth = Socks5Auth{"", "x"};
    EXPECT_THROW(socks5_connect(o, "203.0.113.7", 443), std::invalid_argument);
    o.auth = Socks5Auth{std::string(256, 'u'), "x"};
    EXPECT_THROW(socks5_connect(o, "203.0.113.7", 443), std::invalid_argument);
}

TEST(Socks5, RetryIsBoundedAndGivesUpOnTransientFailure) {
    Socks5Options o;
    o.proxy_host = "127.0.0.1";
    o.proxy_port = 1;  // refused every time
    o.timeout = std::chrono::milliseconds(200);
    try {
        socks5_connect_with_retry(o, "203.0.113.7", 443, 3, std::chrono::milliseconds(10));
        FAIL() << "expected failure";
    } catch (const Socks5Error& e) {
        EXPECT_TRUE(e.transient());
    }
}

TEST(Socks5, RetryRejectsZeroAttempts) {
    Socks5Options o;
    o.proxy_host = "127.0.0.1";
    EXPECT_THROW(socks5_connect_with_retry(o, "203.0.113.7", 443, 0, std::chrono::milliseconds(1)),
                 std::invalid_argument);
}

TEST(Socks5Connection, ConnectionTunnelsThroughProxyAndReportsRejectionAsFinal) {
    {
        FakeProxy proxy(FakeProxy::Mode::Ok);
        cppgram::mtproto::Connection conn(cppgram::mtproto::TransportKind::Intermediate);
        conn.connect_via_socks5(opts_for(proxy), "203.0.113.7", 443);
        EXPECT_TRUE(conn.is_open());
        conn.close();
        proxy.join();
        EXPECT_EQ(proxy.target, "203.0.113.7");
        EXPECT_EQ(proxy.target_port, 443);
    }
    {
        FakeProxy proxy(FakeProxy::Mode::RejectAuth);
        Socks5Options o = opts_for(proxy);
        o.auth = Socks5Auth{"alice", "wrong"};
        cppgram::mtproto::Connection conn(cppgram::mtproto::TransportKind::Intermediate);
        try {
            conn.connect_via_socks5(o, "203.0.113.7", 443);
            FAIL() << "expected rejection";
        } catch (const cppgram::mtproto::ProxyError& e) {
            EXPECT_FALSE(e.transient());
        }
        EXPECT_FALSE(conn.is_open());
    }
}
