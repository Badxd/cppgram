// cppgram - blocking TCP connection that moves MTProto packets through a transport codec (POSIX sockets).
#pragma once

#include <cstdint>
#include <string>

#include "cppgram/mtproto/transport.h"

namespace cppgram::mtproto {

class ConnectionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ConnectionTimeout : public ConnectionError {
public:
    using ConnectionError::ConnectionError;
};

// Anything that can exchange whole MTProto packets (already decrypted or plain, depending on the layer).
class PacketChannel {
public:
    virtual ~PacketChannel() = default;
    virtual void send_packet(const Bytes& packet) = 0;
    virtual Bytes receive_packet() = 0;
};

class Connection : public PacketChannel {
public:
    explicit Connection(TransportKind kind = TransportKind::Intermediate);
    ~Connection() override;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void connect(const std::string& ipv4, std::uint16_t port);
    // Receive timeout in milliseconds (0 = block forever). Expiry throws ConnectionTimeout.
    void set_receive_timeout(int milliseconds);
    void close() noexcept;
    bool is_open() const noexcept { return fd_ >= 0; }

    void send_packet(const Bytes& packet) override;
    Bytes receive_packet() override;

private:
    void send_all(const Bytes& data);

    Transport transport_;
    int fd_ = -1;
    bool init_sent_ = false;
};

}  // namespace cppgram::mtproto
