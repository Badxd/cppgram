#include "cppgram/mtproto/connection.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace cppgram::mtproto {
namespace {

// Opens a blocking TCP socket to host:port. The connect itself runs non-blocking and is bounded by `timeout`
// per candidate address. Throws ConnectionError, or ConnectionTimeout when every candidate timed out.
int tcp_connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string port_text = std::to_string(port);
    const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &found);
    if (rc != 0) {
        throw ConnectionError("resolve " + host + ": " + ::gai_strerror(rc));
    }
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(found, ::freeaddrinfo);

    std::string last_error = "no usable address";
    bool timed_out = false;
    const int timeout_ms = static_cast<int>(timeout.count() <= 0 ? 10000 : timeout.count());
    for (addrinfo* ai = found; ai != nullptr; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) {
            last_error = std::strerror(errno);
            continue;
        }
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            last_error = std::strerror(errno);
            ::close(fd);
            continue;
        }
        const int r = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r != 0 && errno != EINPROGRESS) {
            last_error = std::strerror(errno);
            ::close(fd);
            continue;
        }
        if (r != 0) {
            pollfd waiter{fd, POLLOUT, 0};
            int pr = 0;
            do {
                pr = ::poll(&waiter, 1, timeout_ms);
            } while (pr < 0 && errno == EINTR);
            if (pr == 0) {
                timed_out = true;
                last_error = "timed out";
                ::close(fd);
                continue;
            }
            if (pr < 0) {
                last_error = std::strerror(errno);
                ::close(fd);
                continue;
            }
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
                last_error = std::strerror(so_error != 0 ? so_error : errno);
                ::close(fd);
                continue;
            }
        }
        if (::fcntl(fd, F_SETFL, flags) != 0) {
            last_error = std::strerror(errno);
            ::close(fd);
            continue;
        }
        return fd;
    }
    if (timed_out) {
        throw ConnectionTimeout("connect " + host + ":" + port_text + ": timed out");
    }
    throw ConnectionError("connect " + host + ":" + port_text + ": " + last_error);
}

}  // namespace

Connection::Connection(TransportKind kind) : transport_(kind) {}

Connection::~Connection() {
    close();
}

void Connection::connect(const std::string& ipv4, std::uint16_t port) {
    close();
    sockaddr_in literal{};
    if (inet_pton(AF_INET, ipv4.c_str(), &literal.sin_addr) != 1) {
        throw ConnectionError("invalid IPv4 address: " + ipv4);
    }
    fd_ = tcp_connect(ipv4, port, std::chrono::milliseconds(connect_timeout_ms_));
    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    init_sent_ = false;
    transport_ = Transport(transport_.kind());
}

void Connection::connect_via_socks5(const Socks5Options& proxy, const std::string& ipv4, std::uint16_t port) {
    close();
    int fd = -1;
    try {
        fd = socks5_connect(proxy, ipv4, port);
    } catch (const Socks5Error& e) {
        throw ProxyError(e.what(), e.transient());
    } catch (const std::invalid_argument& e) {
        throw ProxyError(e.what(), false);
    }
    fd_ = fd;
    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    init_sent_ = false;
    transport_ = Transport(transport_.kind());
}

void Connection::connect_obfuscated(const std::string& proxy_host, std::uint16_t proxy_port,
                                    const ProxySecret& secret, std::int16_t dc_id,
                                    std::chrono::milliseconds timeout) {
    if (secret.kind == ProxySecretKind::FakeTls) {
        throw std::invalid_argument("fake-TLS (ee) proxy secrets are not supported");
    }
    close();
    try {
        fd_ = tcp_connect(proxy_host, proxy_port, timeout);
    } catch (const ConnectionError& e) {
        throw ProxyError(e.what(), true);
    }
    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    const ObfuscatedFraming framing = secret.kind == ProxySecretKind::PaddedIntermediate
                                          ? ObfuscatedFraming::PaddedIntermediate
                                          : ObfuscatedFraming::Intermediate;
    obfuscated_.emplace(ObfuscatedClient::create(framing, dc_id, secret.key));
    init_sent_ = true;  // the protocol tag is inside the obfuscated handshake, never sent as plain bytes
    transport_ = Transport(TransportKind::Intermediate);
    try {
        send_all(obfuscated_->handshake());
    } catch (const ConnectionError& e) {
        close();
        throw ProxyError(e.what(), true);
    }
}

void Connection::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    obfuscated_.reset();
}

void Connection::send_all(const Bytes& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) throw ConnectionTimeout("send timed out");
            throw ConnectionError(std::string("send: ") + std::strerror(errno));
        }
        sent += static_cast<std::size_t>(n);
    }
}

void Connection::send_packet(const Bytes& packet) {
    if (fd_ < 0) {
        throw ConnectionError("not connected");
    }
    if (obfuscated_) {
        send_all(obfuscated_->encrypt(obfuscated_->frame(packet)));
        return;
    }
    Bytes out;
    if (!init_sent_) {
        out = transport_.init_bytes();
        init_sent_ = true;
    }
    const Bytes frame = transport_.encode(packet);
    out.insert(out.end(), frame.begin(), frame.end());
    send_all(out);
}

void Connection::set_receive_timeout(int milliseconds) {
    if (fd_ < 0) {
        throw ConnectionError("not connected");
    }
    timeval tv{};
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
        throw ConnectionError(std::string("setsockopt: ") + std::strerror(errno));
    }
    io_timeout_ms_ = milliseconds;
}

Bytes Connection::receive_packet() {
    if (fd_ < 0) {
        throw ConnectionError("not connected");
    }
    for (;;) {
        if (auto packet = transport_.next()) {
            return std::move(*packet);
        }
        std::uint8_t buf[4096];
        const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) throw ConnectionTimeout("receive timed out");
            throw ConnectionError(std::string("recv: ") + std::strerror(errno));
        }
        if (n == 0) {
            throw ConnectionError("connection closed by server");
        }
        Bytes chunk(buf, buf + n);
        if (obfuscated_) {
            chunk = obfuscated_->decrypt(chunk);
        }
        transport_.feed(chunk);
    }
}

}  // namespace cppgram::mtproto
