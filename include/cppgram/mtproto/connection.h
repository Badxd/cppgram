// cppgram - blocking TCP connection that moves MTProto packets through a transport codec (POSIX sockets).
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>

#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/socks5.h"
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

// A SOCKS5 or MTProto proxy refused or failed the tunnel. transient() is false for rejected credentials and
// refused targets.
class ProxyError : public ConnectionError {
public:
    ProxyError(const std::string& message, bool transient) : ConnectionError(message), transient_(transient) {}
    bool transient() const noexcept { return transient_; }

private:
    bool transient_;
};

// Anything that can exchange whole MTProto packets (already decrypted or plain, depending on the layer).
class PacketChannel {
public:
    virtual ~PacketChannel() = default;
    virtual void send_packet(const Bytes& packet) = 0;
    virtual Bytes receive_packet() = 0;
    // Like receive_packet(), but returns early by throwing OperationCancelled once `stop` is requested. The default
    // ignores `stop`, so a channel that can block must override it to be cancellable.
    // `deadline` bounds the whole wait; the default cannot honour it and only checks `stop` before blocking.
    virtual Bytes receive_packet_cancellable(const std::stop_token& stop,
                                             std::chrono::steady_clock::time_point deadline) {
        (void)stop;
        (void)deadline;
        return receive_packet();
    }
};

class Connection : public PacketChannel {
public:
    explicit Connection(TransportKind kind = TransportKind::Intermediate);
    ~Connection() override;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Direct TCP connection. Failures throw ConnectionError; a connect that does not finish within
    // set_connect_timeout() throws ConnectionTimeout.
    void connect(const std::string& ipv4, std::uint16_t port);
    // Connects to ipv4:port through a SOCKS5 proxy. Failures throw ProxyError.
    void connect_via_socks5(const Socks5Options& proxy, const std::string& ipv4, std::uint16_t port);
    // Connects to an MTProto proxy (obfuscated transport) and sends the 64-byte handshake for dc_id.
    // The secret must be a plain or padded-intermediate secret; fake-TLS secrets throw std::invalid_argument.
    // Connect failures and timeouts throw ProxyError with transient() == true.
    // The framing follows the secret: plain secrets use intermediate, dd secrets use padded intermediate.
    void connect_obfuscated(const std::string& proxy_host, std::uint16_t proxy_port, const ProxySecret& secret,
                            std::int16_t dc_id, std::chrono::milliseconds timeout);
    // Same, with the framing chosen by the caller. Padded intermediate needs a dd secret, and a dd secret needs padded
    // intermediate; any other combination throws std::invalid_argument before the network is used. Abridged is allowed
    // with a plain secret only.
    void connect_obfuscated(const std::string& proxy_host, std::uint16_t proxy_port, const ProxySecret& secret,
                            std::int16_t dc_id, std::chrono::milliseconds timeout, ObfuscatedFraming framing);
    // Timeout for establishing a TCP connection, in milliseconds (default 10000).
    void set_connect_timeout(int milliseconds) noexcept { connect_timeout_ms_ = milliseconds; }
    // Receive and send timeout in milliseconds (0 = block forever). Expiry throws ConnectionTimeout.
    void set_receive_timeout(int milliseconds);
    void close() noexcept;
    bool is_open() const noexcept { return fd_ >= 0; }

    void send_packet(const Bytes& packet) override;
    Bytes receive_packet() override;
    // Polls in short slices so a stop request is seen promptly. Receive timeouts still apply, measured from the
    // last byte received. A token that can never stop uses the blocking receive_packet() path.
    Bytes receive_packet_cancellable(const std::stop_token& stop,
                                     std::chrono::steady_clock::time_point deadline) override;

private:
    void send_all(const Bytes& data);
    // Receive failures. Before the proxy has sent any byte, a timeout or a close is reported as a ProxyError that names
    // the likely causes (secret, framing, DC id, proxy upstream). After that they keep their usual types.
    void throw_receive_timeout();
    void throw_receive_closed();

    Transport transport_;
    int fd_ = -1;
    bool init_sent_ = false;
    int connect_timeout_ms_ = 10000;
    int io_timeout_ms_ = 0;
    std::optional<ObfuscatedClient> obfuscated_;
    std::size_t obfuscated_rx_bytes_ = 0;  // encrypted bytes received from the proxy since the handshake
};

}  // namespace cppgram::mtproto
