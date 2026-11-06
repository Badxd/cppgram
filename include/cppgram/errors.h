#pragma once

/**
 * @file errors.h
 * @brief Typed error hierarchy with structured context, and categorisation of every error this library throws.
 *
 * The new classes live in cppgram::errors. The existing error types (cppgram::mtproto::RpcError, FloodWaitError,
 * AuthError, ConnectionError, ... and cppgram::bot::TgException) are unchanged. categorize() maps them to a Category,
 * so callers can branch on the category without parsing messages.
 */

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cppgram::errors {

enum class Category : std::uint8_t {
    Unknown,
    Network,
    Timeout,
    Authentication,
    Authorization,
    Rpc,
    BotApi,
    FloodWait,
    Session,
    Serialization,
    Transport,
    FileTransfer,
    Cancelled,
    Crypto,
    Proxy,
};

/// Stable lower-case name, for example "flood_wait". Never changes for an existing value.
std::string_view to_string(Category category) noexcept;

/// Structured fields attached to an error. Every field is optional and may be empty.
struct Context {
    std::string method; ///< API method or RPC name, for example "sendMessage"
    std::optional<std::int64_t> code; ///< Telegram or Bot API error code
    std::string request_id; ///< Correlation id, when the caller supplies one
    std::optional<std::chrono::seconds> retry_after; ///< How long to wait before retrying
    std::optional<int> dc_id; ///< Data centre involved, when known
};

/**
 * @brief Base class of the new error types. Derives from std::runtime_error, so catch (std::runtime_error&) still works.
 */
class Error : public std::runtime_error {
public:
    Error(Category category, const std::string& message, Context context = {})
        : std::runtime_error(message)
        , category_(category)
        , context_(std::move(context)) {
    }

    Category category() const noexcept {
        return category_;
    }
    const Context& context() const noexcept {
        return context_;
    }
    const std::string& method() const noexcept {
        return context_.method;
    }
    std::optional<std::int64_t> code() const noexcept {
        return context_.code;
    }
    const std::string& request_id() const noexcept {
        return context_.request_id;
    }
    std::optional<std::chrono::seconds> retry_after() const noexcept {
        return context_.retry_after;
    }

private:
    Category category_;
    Context context_;
};

class NetworkError : public Error {
public:
    explicit NetworkError(const std::string& message, Context context = {})
        : Error(Category::Network, message, std::move(context)) {
    }
};

class TimeoutError : public Error {
public:
    explicit TimeoutError(const std::string& message, Context context = {})
        : Error(Category::Timeout, message, std::move(context)) {
    }
};

/// Login or credentials were rejected, or a Web App signature did not verify.
class AuthenticationError : public Error {
public:
    explicit AuthenticationError(const std::string& message, Context context = {})
        : Error(Category::Authentication, message, std::move(context)) {
    }
};

/// The account or bot is not allowed to perform the operation.
class AuthorizationError : public Error {
public:
    explicit AuthorizationError(const std::string& message, Context context = {})
        : Error(Category::Authorization, message, std::move(context)) {
    }
};

class RpcError : public Error {
public:
    explicit RpcError(const std::string& message, Context context = {})
        : Error(Category::Rpc, message, std::move(context)) {
    }
};

class BotApiError : public Error {
public:
    explicit BotApiError(const std::string& message, Context context = {})
        : Error(Category::BotApi, message, std::move(context)) {
    }
};

class FloodWaitError : public Error {
public:
    explicit FloodWaitError(const std::string& message, Context context = {})
        : Error(Category::FloodWait, message, std::move(context)) {
    }
};

class SessionError : public Error {
public:
    explicit SessionError(const std::string& message, Context context = {})
        : Error(Category::Session, message, std::move(context)) {
    }
};

class SerializationError : public Error {
public:
    explicit SerializationError(const std::string& message, Context context = {})
        : Error(Category::Serialization, message, std::move(context)) {
    }
};

class TransportError : public Error {
public:
    explicit TransportError(const std::string& message, Context context = {})
        : Error(Category::Transport, message, std::move(context)) {
    }
};

class FileTransferError : public Error {
public:
    explicit FileTransferError(const std::string& message, Context context = {})
        : Error(Category::FileTransfer, message, std::move(context)) {
    }
};

class CancelledError : public Error {
public:
    explicit CancelledError(const std::string& message, Context context = {})
        : Error(Category::Cancelled, message, std::move(context)) {
    }
};

/**
 * @brief Category of any exception. Errors of this library are classified by type. Other exceptions are Unknown.
 * Never throws.
 */
Category categorize(const std::exception& e) noexcept;

/**
 * @brief Structured context of any exception. Fields come from the error where the library records them, otherwise
 * they are empty. Can throw std::bad_alloc while copying strings.
 */
Context context_of(const std::exception& e);

} // namespace cppgram::errors
