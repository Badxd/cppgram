#include "cppgram/mtproto/socks5.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>

namespace cppgram::mtproto {
namespace {

constexpr std::uint8_t kVersion = 5;
constexpr std::uint8_t kMethodNone = 0x00;
constexpr std::uint8_t kMethodUserPass = 0x02;
constexpr std::uint8_t kMethodNoneAcceptable = 0xFF;
constexpr std::uint8_t kCmdConnect = 0x01;
constexpr std::uint8_t kAtypIPv4 = 0x01;
constexpr std::uint8_t kAtypDomain = 0x03;
constexpr std::uint8_t kAtypIPv6 = 0x04;

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int get() const noexcept { return fd_; }
    int release() noexcept { int f = fd_; fd_ = -1; return f; }

private:
    int fd_;
};

const char* reply_text(std::uint8_t rep) {
    switch (rep) {
        case 0x01: return "general SOCKS server failure";
        case 0x02: return "connection not allowed by ruleset";
        case 0x03: return "network unreachable";
        case 0x04: return "host unreachable";
        case 0x05: return "connection refused";
        case 0x06: return "TTL expired";
        case 0x07: return "command not supported";
        case 0x08: return "address type not supported";
        default: return "unknown reply";
    }
}

void set_socket_timeouts(int fd, std::chrono::milliseconds timeout) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

// Connects one resolved address. Returns -1 on failure. The returned socket is blocking with timeouts set.
int connect_one(const addrinfo* ai, std::chrono::milliseconds timeout) {
    const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
    if (fd < 0) return -1;
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    if (rc != 0) {
        pollfd pfd{fd, POLLOUT, 0};
        if (::poll(&pfd, 1, static_cast<int>(timeout.count())) <= 0) {
            ::close(fd);
            return -1;
        }
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            ::close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, flags);  // back to blocking; the timeouts below bound each read and write
    set_socket_timeouts(fd, timeout);
    return fd;
}

void send_all(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) throw Socks5Error("timed out sending to SOCKS5 proxy", 0xFF, true);
            throw Socks5Error("failed sending to SOCKS5 proxy: " + std::string(std::strerror(errno)), 0xFF, true);
        }
        sent += static_cast<std::size_t>(n);
    }
}

void recv_exact(int fd, std::uint8_t* out, std::size_t len) {
    std::size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd, out + got, len - got, 0);
        if (n == 0) throw Socks5Error("SOCKS5 proxy closed the connection during handshake", 0xFF, true);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) throw Socks5Error("timed out waiting for SOCKS5 proxy", 0xFF, true);
            throw Socks5Error("failed reading from SOCKS5 proxy: " + std::string(std::strerror(errno)), 0xFF, true);
        }
        got += static_cast<std::size_t>(n);
    }
}

// Encodes the target as SOCKS5 ATYP + address. IPv4 and IPv6 literals are sent as binary; anything else is a domain.
std::vector<std::uint8_t> encode_target(std::string_view host, std::uint16_t port) {
    std::vector<std::uint8_t> out;
    in_addr v4{};
    in6_addr v6{};
    const std::string h(host);
    if (inet_pton(AF_INET, h.c_str(), &v4) == 1) {
        out.push_back(kAtypIPv4);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v4);
        out.insert(out.end(), p, p + 4);
    } else if (inet_pton(AF_INET6, h.c_str(), &v6) == 1) {
        out.push_back(kAtypIPv6);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v6);
        out.insert(out.end(), p, p + 16);
    } else {
        if (host.empty() || host.size() > 255) {
            throw std::invalid_argument("SOCKS5 target host must be 1..255 bytes");
        }
        out.push_back(kAtypDomain);
        out.push_back(static_cast<std::uint8_t>(host.size()));
        out.insert(out.end(), host.begin(), host.end());
    }
    out.push_back(static_cast<std::uint8_t>(port >> 8));
    out.push_back(static_cast<std::uint8_t>(port & 0xFF));
    return out;
}

void check_auth(const Socks5Auth& auth) {
    if (auth.username.empty() || auth.username.size() > 255 || auth.password.empty() || auth.password.size() > 255) {
        throw std::invalid_argument("SOCKS5 username and password must each be 1..255 bytes");
    }
}

}  // namespace

int socks5_connect(const Socks5Options& options, std::string_view target_host, std::uint16_t target_port) {
    if (options.auth) check_auth(*options.auth);
    const auto target = encode_target(target_host, target_port);  // validates the target before any network use
    if (options.proxy_host.empty()) throw std::invalid_argument("SOCKS5 proxy host is empty");

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const std::string port = std::to_string(options.proxy_port);
    const int gai = getaddrinfo(options.proxy_host.c_str(), port.c_str(), &hints, &list);
    if (gai != 0) throw Socks5Error("cannot resolve SOCKS5 proxy: " + std::string(gai_strerror(gai)), 0xFF, true);

    Fd sock;
    for (const addrinfo* ai = list; ai != nullptr && sock.get() < 0; ai = ai->ai_next) {
        sock = Fd(connect_one(ai, options.timeout));
    }
    freeaddrinfo(list);
    if (sock.get() < 0) throw Socks5Error("cannot connect to SOCKS5 proxy (refused or timed out)", 0xFF, true);

    // Greeting: offer no-auth, plus username/password when credentials are configured.
    std::vector<std::uint8_t> greeting{kVersion};
    if (options.auth) {
        greeting.insert(greeting.end(), {2, kMethodNone, kMethodUserPass});
    } else {
        greeting.insert(greeting.end(), {1, kMethodNone});
    }
    send_all(sock.get(), greeting);
    std::uint8_t choice[2];
    recv_exact(sock.get(), choice, 2);
    if (choice[0] != kVersion) throw Socks5Error("SOCKS5 proxy sent a wrong version");
    if (choice[1] == kMethodNoneAcceptable) throw Socks5Error("SOCKS5 proxy accepts no offered auth method");
    if (choice[1] == kMethodUserPass) {
        if (!options.auth) throw Socks5Error("SOCKS5 proxy requires authentication but none is configured");
        std::vector<std::uint8_t> auth{1, static_cast<std::uint8_t>(options.auth->username.size())};
        auth.insert(auth.end(), options.auth->username.begin(), options.auth->username.end());
        auth.push_back(static_cast<std::uint8_t>(options.auth->password.size()));
        auth.insert(auth.end(), options.auth->password.begin(), options.auth->password.end());
        send_all(sock.get(), auth);
        std::uint8_t status[2];
        recv_exact(sock.get(), status, 2);
        if (status[1] != 0) throw Socks5Error("SOCKS5 username/password rejected");
    } else if (choice[1] != kMethodNone) {
        throw Socks5Error("SOCKS5 proxy chose an unsupported auth method");
    }

    // CONNECT request.
    std::vector<std::uint8_t> request{kVersion, kCmdConnect, 0};
    request.insert(request.end(), target.begin(), target.end());
    send_all(sock.get(), request);

    // Reply: VER REP RSV ATYP BND.ADDR BND.PORT. The bound address is read only to keep the stream aligned.
    std::uint8_t head[4];
    recv_exact(sock.get(), head, 4);
    if (head[0] != kVersion) throw Socks5Error("SOCKS5 proxy sent a wrong version in reply");
    if (head[1] != 0) {
        throw Socks5Error(std::string("SOCKS5 connect failed: ") + reply_text(head[1]), head[1],
                          head[1] == 0x01 || head[1] == 0x03 || head[1] == 0x04 || head[1] == 0x06);
    }
    std::size_t bound_len = 0;
    switch (head[3]) {
        case kAtypIPv4: bound_len = 4; break;
        case kAtypIPv6: bound_len = 16; break;
        case kAtypDomain: {
            std::uint8_t len = 0;
            recv_exact(sock.get(), &len, 1);
            bound_len = len;
            break;
        }
        default: throw Socks5Error("SOCKS5 proxy sent an unknown address type in reply");
    }
    std::vector<std::uint8_t> bound(bound_len + 2);
    recv_exact(sock.get(), bound.data(), bound.size());
    return sock.release();
}

int socks5_connect_with_retry(const Socks5Options& options, std::string_view target_host, std::uint16_t target_port,
                              unsigned attempts, std::chrono::milliseconds pause) {
    if (attempts == 0) throw std::invalid_argument("SOCKS5 retry needs at least one attempt");
    for (unsigned i = 1;; ++i) {
        try {
            return socks5_connect(options, target_host, target_port);
        } catch (const Socks5Error& e) {
            // Refusals and rejected credentials are final; only transient network trouble is retried.
            if (!e.transient() || i >= attempts) throw;
        }
        std::this_thread::sleep_for(pause);
    }
}

}  // namespace cppgram::mtproto
