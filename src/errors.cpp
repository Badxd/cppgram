#include "cppgram/errors.h"

#include "cppgram/bot/TgException.h"
#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/cancel.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/tl.h"
#include "cppgram/mtproto/transport.h"

namespace cppgram::errors {

std::string_view to_string(Category category) noexcept {
    switch (category) {
    case Category::Unknown:
        return "unknown";
    case Category::Network:
        return "network";
    case Category::Timeout:
        return "timeout";
    case Category::Authentication:
        return "authentication";
    case Category::Authorization:
        return "authorization";
    case Category::Rpc:
        return "rpc";
    case Category::BotApi:
        return "bot_api";
    case Category::FloodWait:
        return "flood_wait";
    case Category::Session:
        return "session";
    case Category::Serialization:
        return "serialization";
    case Category::Transport:
        return "transport";
    case Category::FileTransfer:
        return "file_transfer";
    case Category::Cancelled:
        return "cancelled";
    case Category::Crypto:
        return "crypto";
    case Category::Proxy:
        return "proxy";
    }
    return "unknown";
}

namespace {

// Order matters: more derived legacy types are tested before their bases.
Category legacy_category(const std::exception& e) noexcept {
    if (dynamic_cast<const mtproto::OperationCancelled*>(&e))
        return Category::Cancelled;
    if (dynamic_cast<const mtproto::FloodWaitError*>(&e))
        return Category::FloodWait;
    if (dynamic_cast<const mtproto::RpcError*>(&e))
        return Category::Rpc;
    if (dynamic_cast<const mtproto::ConnectionTimeout*>(&e))
        return Category::Timeout;
    if (dynamic_cast<const mtproto::ProxyError*>(&e))
        return Category::Proxy;
    if (dynamic_cast<const mtproto::ConnectionError*>(&e))
        return Category::Network;
    if (dynamic_cast<const mtproto::AuthError*>(&e))
        return Category::Authentication;
    if (dynamic_cast<const mtproto::TransportError*>(&e))
        return Category::Transport;
    if (dynamic_cast<const mtproto::TlError*>(&e))
        return Category::Serialization;
    if (dynamic_cast<const mtproto::SessionCorruptError*>(&e))
        return Category::Session;
    if (dynamic_cast<const mtproto::CryptoError*>(&e))
        return Category::Crypto;
    if (const auto* bot = dynamic_cast<const bot::TgException*>(&e)) {
        return bot->errorCode == bot::TgException::ErrorCode::Flood ? Category::FloodWait : Category::BotApi;
    }
    return Category::Unknown;
}

} // namespace

Category categorize(const std::exception& e) noexcept {
    if (const auto* own = dynamic_cast<const Error*>(&e))
        return own->category();
    return legacy_category(e);
}

Context context_of(const std::exception& e) {
    if (const auto* own = dynamic_cast<const Error*>(&e))
        return own->context();

    Context context;
    if (const auto* flood = dynamic_cast<const mtproto::FloodWaitError*>(&e)) {
        context.code = flood->code();
        context.retry_after = std::chrono::seconds(flood->seconds());
    } else if (const auto* rpc = dynamic_cast<const mtproto::RpcError*>(&e)) {
        context.code = rpc->code();
    } else if (const auto* bot = dynamic_cast<const bot::TgException*>(&e)) {
        context.code = static_cast<std::int64_t>(bot->errorCode);
        if (bot->parameters && bot->parameters->retryAfter) {
            context.retry_after = std::chrono::seconds(*bot->parameters->retryAfter);
        }
    }
    return context;
}

} // namespace cppgram::errors
