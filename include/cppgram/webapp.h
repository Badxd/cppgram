#pragma once

/**
 * @file webapp.h
 * @brief Verification of Telegram Web App initData, following Telegram's documented algorithm.
 *
 * Algorithm (https://core.telegram.org/bots/webapps#validating-data-received-via-the-mini-app):
 *   1. initData is a URL query string. Split on '&', split each pair on the first '=', percent-decode key and value
 *      ('+' is a space).
 *   2. Remove the "hash" field. Sort the remaining fields by key and join them as "key=value" lines with '\n'.
 *   3. secret_key = HMAC-SHA256(key = "WebAppData", message = bot_token).
 *   4. expected = hex(HMAC-SHA256(key = secret_key, message = data_check_string)).
 *   5. Compare expected with "hash" in constant time.
 *
 * Nothing in InitData is trusted until the signature verifies. The bot token is never stored, logged or echoed.
 */

#include "cppgram/errors.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace cppgram::webapp {

enum class Status : std::uint8_t {
    Valid,
    Empty, ///< No input, or only "?"
    Malformed, ///< Bad pair structure, empty key, empty segment, or a hash that is not 64 hex characters
    BadEncoding, ///< Invalid percent escape
    DuplicateField, ///< The same key appears more than once (after decoding)
    MissingHash,
    MissingAuthDate,
    BadAuthDate, ///< auth_date is not a decimal integer
    BadSignature, ///< hash does not match, or the bot token is empty
    Expired, ///< auth_date is older than VerifyOptions::max_age
    FromFuture, ///< auth_date is later than now + VerifyOptions::max_future_skew
    BadUser, ///< The user field is present but is not a JSON object with an integer id
};

/// Stable lower-case name, for example "bad_signature".
std::string_view to_string(Status status) noexcept;

struct User {
    std::int64_t id = 0;
    bool is_bot = false;
    std::string first_name;
    std::optional<std::string> last_name;
    std::optional<std::string> username;
    std::optional<std::string> language_code;
};

/// Only produced after the signature and the time checks pass.
struct InitData {
    std::int64_t auth_date = 0;
    std::optional<std::string> query_id;
    std::optional<std::string> chat_type;
    std::optional<std::string> chat_instance;
    std::optional<std::string> start_param;
    std::optional<User> user;
    /// Every decoded field except "hash", in key order.
    std::map<std::string, std::string> fields;
};

struct VerifyOptions {
    std::chrono::seconds max_age { 86400 }; ///< Reject auth_date older than this (default 24 h)
    std::chrono::seconds max_future_skew { 60 }; ///< Allow auth_date this far ahead of the local clock
};

struct Result {
    Status status = Status::Empty;
    std::optional<InitData> data; ///< Set only when status is Valid
    bool ok() const noexcept {
        return status == Status::Valid;
    }
};

/// Verifies against the given Unix time. Use this in tests and when the caller owns the clock.
Result verify(std::string_view init_data, std::string_view bot_token, std::int64_t now_unix,
              const VerifyOptions& options = {});

/// Verifies against the system clock.
/**
 * Validation rules beyond the documented algorithm, kept on purpose:
 *  - Duplicate keys are rejected (DuplicateField), checked after percent-decoding. Telegram does not say what a
 *    repeated key means, so a repeat is treated as tampering rather than guessed at.
 *  - data-check-string is built from the keys sorted by byte order (std::map order).
 *  - The user object is checked for the documented minimum only: "id" (integer) and "first_name" (string).
 *    Optional user fields of an unexpected type are ignored, not rejected.
 */
Result verify(std::string_view init_data, std::string_view bot_token, const VerifyOptions& options = {});

/// Like verify(), but throws errors::AuthenticationError on any failure. The message is the status name only.
InitData verify_or_throw(std::string_view init_data, std::string_view bot_token, const VerifyOptions& options = {});

/// Compares two strings without an early exit on the first difference. Lengths are compared first, which is not secret.
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;

/// Lower-case hex HMAC-SHA256. Exposed for tests and for building the signing values.
std::string hmac_sha256_hex(std::string_view key, std::string_view message);

} // namespace cppgram::webapp
