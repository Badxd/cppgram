#include "cppgram/mtproto/connection.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace cppgram::mtproto {

Connection::Connection(TransportKind kind) : transport_(kind) {}

Connection::~Connection() {
    close();
}

void Connection::connect(const std::string& ipv4, std::uint16_t port) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw ConnectionError(std::string("socket: ") + std::strerror(errno));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &addr.sin_addr) != 1) {
        close();
        throw ConnectionError("invalid IPv4 address: " + ipv4);
    }
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string err = std::strerror(errno);
        close();
        throw ConnectionError("connect " + ipv4 + ": " + err);
    }
    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    init_sent_ = false;
    transport_ = Transport(transport_.kind());
}

void Connection::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Connection::send_all(const Bytes& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw ConnectionError(std::string("send: ") + std::strerror(errno));
        }
        sent += static_cast<std::size_t>(n);
    }
}

void Connection::send_packet(const Bytes& packet) {
    if (fd_ < 0) {
        throw ConnectionError("not connected");
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
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        throw ConnectionError(std::string("setsockopt: ") + std::strerror(errno));
    }
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
        transport_.feed(Bytes(buf, buf + n));
    }
}

}  // namespace cppgram::mtproto
