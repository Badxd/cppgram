#include "cppgram/errors.h"

#include "cppgram/bot/TgException.h"
#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/tl.h"
#include "cppgram/mtproto/transport.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace cppgram;
using errors::Category;

TEST(ErrorsTest, CategoryNamesAreStable) {
    EXPECT_EQ(errors::to_string(Category::FloodWait), "flood_wait");
    EXPECT_EQ(errors::to_string(Category::BotApi), "bot_api");
    EXPECT_EQ(errors::to_string(Category::Unknown), "unknown");
}

TEST(ErrorsTest, NewTypesCarryCategoryAndContext) {
    errors::Context ctx;
    ctx.method = "sendMessage";
    ctx.code = 429;
    ctx.request_id = "req-1";
    ctx.retry_after = std::chrono::seconds(12);
    errors::FloodWaitError e("slow down", ctx);

    EXPECT_EQ(e.category(), Category::FloodWait);
    EXPECT_STREQ(e.what(), "slow down");
    EXPECT_EQ(e.method(), "sendMessage");
    ASSERT_TRUE(e.code().has_value());
    EXPECT_EQ(*e.code(), 429);
    EXPECT_EQ(e.request_id(), "req-1");
    ASSERT_TRUE(e.retry_after().has_value());
    EXPECT_EQ(e.retry_after()->count(), 12);
}

TEST(ErrorsTest, EveryNewTypeHasItsOwnCategory) {
    EXPECT_EQ(errors::categorize(errors::NetworkError("x")), Category::Network);
    EXPECT_EQ(errors::categorize(errors::TimeoutError("x")), Category::Timeout);
    EXPECT_EQ(errors::categorize(errors::AuthenticationError("x")), Category::Authentication);
    EXPECT_EQ(errors::categorize(errors::AuthorizationError("x")), Category::Authorization);
    EXPECT_EQ(errors::categorize(errors::RpcError("x")), Category::Rpc);
    EXPECT_EQ(errors::categorize(errors::BotApiError("x")), Category::BotApi);
    EXPECT_EQ(errors::categorize(errors::FloodWaitError("x")), Category::FloodWait);
    EXPECT_EQ(errors::categorize(errors::SessionError("x")), Category::Session);
    EXPECT_EQ(errors::categorize(errors::SerializationError("x")), Category::Serialization);
    EXPECT_EQ(errors::categorize(errors::TransportError("x")), Category::Transport);
    EXPECT_EQ(errors::categorize(errors::FileTransferError("x")), Category::FileTransfer);
    EXPECT_EQ(errors::categorize(errors::CancelledError("x")), Category::Cancelled);
}

TEST(ErrorsTest, NewTypesStillDeriveFromRuntimeError) {
    try {
        throw errors::TimeoutError("took too long");
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "took too long");
    }
}

TEST(ErrorsTest, LegacyMtprotoErrorsAreCategorisedWithoutChangingTheirType) {
    EXPECT_EQ(errors::categorize(mtproto::RpcError(400, "PHONE_CODE_INVALID")), Category::Rpc);
    EXPECT_EQ(errors::categorize(mtproto::FloodWaitError(420, "FLOOD_WAIT_30", 30)), Category::FloodWait);
    EXPECT_EQ(errors::categorize(mtproto::AuthError("bad")), Category::Authentication);
    EXPECT_EQ(errors::categorize(mtproto::ConnectionError("reset")), Category::Network);
    EXPECT_EQ(errors::categorize(mtproto::ConnectionTimeout("slow")), Category::Timeout);
    EXPECT_EQ(errors::categorize(mtproto::ProxyError("refused", true)), Category::Proxy);
    EXPECT_EQ(errors::categorize(mtproto::TransportError("bad frame")), Category::Transport);
    EXPECT_EQ(errors::categorize(mtproto::TlError("bad tl")), Category::Serialization);
    EXPECT_EQ(errors::categorize(mtproto::SessionCorruptError("crc")), Category::Session);
    EXPECT_EQ(errors::categorize(mtproto::CryptoError("mac")), Category::Crypto);
}

TEST(ErrorsTest, FloodWaitIsDistinctFromRpcErrorInTheLegacyHierarchy) {
    // FloodWaitError derives from RpcError. Categorisation must pick the more specific one.
    const mtproto::FloodWaitError flood(420, "FLOOD_WAIT_7", 7);
    const std::exception& base = flood;
    EXPECT_EQ(errors::categorize(base), Category::FloodWait);
}

TEST(ErrorsTest, LegacyContextCarriesCodeAndRetryAfter) {
    const mtproto::FloodWaitError flood(420, "FLOOD_WAIT_7", 7);
    const auto ctx = errors::context_of(flood);
    ASSERT_TRUE(ctx.code.has_value());
    EXPECT_EQ(*ctx.code, 420);
    ASSERT_TRUE(ctx.retry_after.has_value());
    EXPECT_EQ(ctx.retry_after->count(), 7);

    const mtproto::RpcError rpc(400, "PHONE_CODE_INVALID");
    const auto rpc_ctx = errors::context_of(rpc);
    ASSERT_TRUE(rpc_ctx.code.has_value());
    EXPECT_EQ(*rpc_ctx.code, 400);
    EXPECT_FALSE(rpc_ctx.retry_after.has_value());
}

TEST(ErrorsTest, BotApiErrorsAreCategorisedAndFloodIsRecognised) {
    const bot::TgException bad("Bad Request: chat not found", bot::TgException::ErrorCode::BadRequest);
    EXPECT_EQ(errors::categorize(bad), Category::BotApi);

    const bot::TgException flood("Too Many Requests", bot::TgException::ErrorCode::Flood);
    EXPECT_EQ(errors::categorize(flood), Category::FloodWait);
    EXPECT_EQ(*errors::context_of(flood).code, 429);
}

TEST(ErrorsTest, UnknownExceptionsAreUnknownWithEmptyContext) {
    const std::runtime_error plain("plain");
    EXPECT_EQ(errors::categorize(plain), Category::Unknown);
    const auto ctx = errors::context_of(plain);
    EXPECT_FALSE(ctx.code.has_value());
    EXPECT_FALSE(ctx.retry_after.has_value());
    EXPECT_TRUE(ctx.method.empty());
}

TEST(ErrorsTest, LegacyMessagesAreUnchanged) {
    // Backward compatibility: the message text the library produces for these types must not change.
    EXPECT_STREQ(mtproto::RpcError(400, "PHONE_CODE_INVALID").what(), "RPC error 400: PHONE_CODE_INVALID");
    EXPECT_EQ(mtproto::FloodWaitError(420, "FLOOD_WAIT_7", 7).seconds(), 7);
    EXPECT_EQ(mtproto::FloodWaitError(420, "FLOOD_WAIT_7", 7).code(), 420);
}
