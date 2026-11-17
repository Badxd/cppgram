// cppgram - SOCKS5 CONNECT tunnel (RFC 1928) with username/password auth (RFC 1929).
// Produces a connected TCP socket that carries the MTProto byte stream unchanged. The transport layer only needs
// a connected fd, so an MTProto obfuscated proxy can later replace this function without touching the framing code.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cppgram::mtproto {

struct Socks5Auth {
    std::string username;  // 1..255 bytes
    std::string password;  // 1..255 bytes
};

struct Socks5Options {
    std::string proxy_host;
    std::uint16_t proxy_port = 1080;
    std::optional<Socks5Auth> auth;  // unset: no-auth method only
    std::chrono::milliseconds timeout{10000};  // applies to connect and to every handshake read/write
};

// Failure of a SOCKS5 exchange. reply_code() is the RFC 1928 REP value when the proxy refused the request
// (for example 5 = connection refused), and 0xFF for errors that happened before a reply (network, timeout, auth).
class Socks5Error : public std::runtime_error {
public:
    Socks5Error(const std::string& message, std::uint8_t reply = 0xFF, bool transient = false)
        : std::runtime_error(message), reply_(reply), transient_(transient) {}
    std::uint8_t reply_code() const noexcept { return reply_; }
    // True for network trouble, timeouts and general proxy failures. Rejected credentials and refused
    // targets are not transient.
    bool transient() const noexcept { return transient_; }

private:
    std::uint8_t reply_;
    bool transient_;
};

// Opens a TCP connection to target_host:target_port through the proxy. target_host may be an IPv4 literal,
// an IPv6 literal, or a domain name (resolved by the proxy). Returns a blocking socket fd owned by the caller.
// Throws std::invalid_argument for a bad target or credentials, and Socks5Error for everything else.
int socks5_connect(const Socks5Options& options, std::string_view target_host, std::uint16_t target_port);

// Retries socks5_connect for transient failures only (network down, timeout, proxy busy or unreachable).
// Authentication and target refusals are returned immediately. Makes at most `attempts` tries with a fixed
// `pause` between them, so it never loops forever.
int socks5_connect_with_retry(const Socks5Options& options, std::string_view target_host, std::uint16_t target_port,
                              unsigned attempts, std::chrono::milliseconds pause);

}  // namespace cppgram::mtproto
