#pragma once

/**
 * @file log.h
 * @brief Structured logging: levels, components, request ids, fields, pluggable sinks and redaction.
 *
 * Logging is off-by-default for debug output: the default level is Warn. The logger never writes secrets. Values of
 * sensitive fields (see is_sensitive_key) are replaced with "***", and free text is passed through redact(), which
 * masks bot tokens and key=value pairs with sensitive keys.
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppgram::log {

enum class Level : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5,
};

/// "TRACE", "DEBUG", "INFO", "WARN", "ERROR" or "OFF".
std::string_view to_string(Level level) noexcept;

/// Case-insensitive parse of the names above. "warning" is accepted for Warn.
std::optional<Level> parse_level(std::string_view name) noexcept;

struct Field {
    std::string key;
    std::string value;
};

/// One log line. Field values and the message are already redacted when a sink receives the record.
struct Record {
    std::chrono::system_clock::time_point time;
    Level level = Level::Info;
    std::string component;
    std::string request_id;
    std::string message;
    std::vector<Field> fields;
};

/// A sink is called while the sink lock is held. It must not call log::write or set_sink.
using Sink = std::function<void(const Record&)>;

/// Minimum level that is written. Thread-safe.
void set_level(Level level) noexcept;
Level level() noexcept;
bool enabled(Level level) noexcept;

/// Replaces the output. An empty function restores the default sink, which writes format(record) to std::clog.
void set_sink(Sink sink);

/**
 * @brief Writes one record if its level is enabled. Redacts the message and field values before the sink sees them.
 * @param request_id Correlation id, empty when none.
 */
void write(Level level, std::string_view component, std::string_view message, std::initializer_list<Field> fields = {},
           std::string_view request_id = {});

/// Formats as "2026-11-04T12:00:00.000Z WARN  [component] rid=abc message key=value ...". Deterministic for a given record.
std::string format(const Record& record);

/// Masks bot tokens ("123456:AAAA...") and key=value pairs whose key is sensitive. Returns the masked copy.
std::string redact(std::string_view text);

/// True for keys whose values must never be logged: api_hash, bot_token, token, password, code, otp, session,
/// session_string, auth_key, secret, srp, hash, phone_code. Case-insensitive, exact match.
bool is_sensitive_key(std::string_view key) noexcept;

} // namespace cppgram::log
