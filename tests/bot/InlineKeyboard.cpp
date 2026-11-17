// Offline Inline Keyboard tests: JSON shape, CallbackQuery parsing, dispatch and the four edit methods.
// Every request goes through a mocked HttpClient. Nothing here reaches Telegram.

#include <gtest/gtest.h>

#include "cppgram/bot/Api.h"
#include "cppgram/bot/EventBroadcaster.h"
#include "cppgram/bot/EventHandler.h"
#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/Types.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using cppgram::bot::InlineKeyboardButton;
using cppgram::bot::InlineKeyboardMarkup;

class HttpClientMock final : public cppgram::bot::HttpClient {
public:
    std::string makeRequest(const std::string& url,
                            std::span<const cppgram::bot::HttpFormField> fields) const override {
        requestUrl = url;
        requestFields.assign(fields.begin(), fields.end());
        return response;
    }

    // Returns the value of a form field as the string Telegram receives. Fails the test when absent.
    std::string field(const std::string& name) const {
        for (const auto& field : requestFields) {
            if (field.name == name)
                return std::get<std::string>(field.value);
        }
        ADD_FAILURE() << "missing form field: " << name;
        return {};
    }

    bool has(const std::string& name) const {
        for (const auto& field : requestFields) {
            if (field.name == name)
                return true;
        }
        return false;
    }

    mutable std::string requestUrl;
    mutable std::vector<cppgram::bot::HttpFormField> requestFields;
    std::string response = R"({"ok":true,"result":true})";
};

std::shared_ptr<InlineKeyboardButton> urlButton(std::string text, std::string url) {
    auto button = std::make_shared<InlineKeyboardButton>();
    button->text = std::move(text);
    button->url = std::move(url);
    return button;
}

std::shared_ptr<InlineKeyboardMarkup> singleButtonMarkup() {
    auto markup = std::make_shared<InlineKeyboardMarkup>();
    markup->inlineKeyboard = { { urlButton("site", "https://example.com") } };
    return markup;
}

} // namespace

// ---- JSON shape of InlineKeyboardMarkup

TEST(InlineKeyboard, MarkupSerializesRowsAndOmitsUnsetForceReply) {
    const auto json = nlohmann::json(*singleButtonMarkup());
    ASSERT_TRUE(json.contains("inline_keyboard"));
    EXPECT_EQ(json.at("inline_keyboard").size(), 1);
    EXPECT_EQ(json.at("inline_keyboard")[0][0].at("url"), "https://example.com");
    EXPECT_FALSE(json.contains("force_reply"));
}

TEST(InlineKeyboard, MarkupPreservesForceReplyRoundTrip) {
    // force_reply is listed on InlineKeyboardMarkup in the Bot API changelog (Aug 24, 2026). It must survive
    // parse and serialize, so an inbound value is never dropped and an outbound value is never invented.
    const auto markup
        = nlohmann::json::parse(R"({"inline_keyboard":[],"force_reply":true})").get<InlineKeyboardMarkup>();
    ASSERT_TRUE(markup.forceReply.has_value());
    EXPECT_TRUE(*markup.forceReply);
    EXPECT_EQ(nlohmann::json(markup).at("force_reply"), true);

    const auto plain = nlohmann::json::parse(R"({"inline_keyboard":[]})").get<InlineKeyboardMarkup>();
    EXPECT_FALSE(plain.forceReply.has_value());
    EXPECT_FALSE(nlohmann::json(plain).contains("force_reply"));
}

TEST(InlineKeyboard, MarkupRequiresInlineKeyboardField) {
    EXPECT_THROW(nlohmann::json::parse(R"({"force_reply":true})").get<InlineKeyboardMarkup>(), std::exception);
}

TEST(InlineKeyboard, EmptyKeyboardRoundTripsAsEmptyArray) {
    InlineKeyboardMarkup markup;
    const auto json = nlohmann::json(markup);
    EXPECT_EQ(json.dump(), R"({"inline_keyboard":[]})");
    EXPECT_TRUE(nlohmann::json::parse(json.dump()).get<InlineKeyboardMarkup>().inlineKeyboard.empty());
}

// ---- Edit methods: keyboard removal and keyboard presence

TEST(InlineKeyboard, EditMessageReplyMarkupRemovalSendsEmptyKeyboard) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    // Passing no markup means "remove the keyboard". Telegram must receive an empty keyboard, not a missing field.
    api.editMessageReplyMarkup(std::int64_t { 42 }, 7, "", nullptr, "");

    ASSERT_TRUE(httpClient.has("reply_markup"));
    EXPECT_EQ(nlohmann::json::parse(httpClient.field("reply_markup")),
              nlohmann::json::parse(R"({"inline_keyboard":[]})"));
}

TEST(InlineKeyboard, EditMessageReplyMarkupSendsKeyboard) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    api.editMessageReplyMarkup(std::int64_t { 42 }, 7, "", singleButtonMarkup(), "");

    EXPECT_EQ(httpClient.field("message_id"), "7");
    const auto markup = nlohmann::json::parse(httpClient.field("reply_markup"));
    EXPECT_EQ(markup.at("inline_keyboard")[0][0].at("url"), "https://example.com");
}

TEST(InlineKeyboard, EditMessageTextSendsKeyboardOrOmitsIt) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    cppgram::bot::EditMessageTextArgs withKeyboard;
    withKeyboard.chatId = std::int64_t { 42 };
    withKeyboard.messageId = 7;
    withKeyboard.text = "edited";
    withKeyboard.replyMarkup = singleButtonMarkup();
    ASSERT_TRUE(api.editMessageText(withKeyboard) == nullptr);
    EXPECT_EQ(httpClient.field("text"), "edited");
    EXPECT_EQ(nlohmann::json::parse(httpClient.field("reply_markup")).at("inline_keyboard")[0][0].at("url"),
              "https://example.com");

    cppgram::bot::EditMessageTextArgs withoutKeyboard;
    withoutKeyboard.chatId = std::int64_t { 42 };
    withoutKeyboard.messageId = 7;
    withoutKeyboard.text = "plain";
    ASSERT_TRUE(api.editMessageText(withoutKeyboard) == nullptr);
    EXPECT_FALSE(httpClient.has("reply_markup"));
}

TEST(InlineKeyboard, EditMessageCaptionSendsKeyboardOrOmitsIt) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    cppgram::bot::EditMessageCaptionArgs withKeyboard;
    withKeyboard.chatId = std::int64_t { 42 };
    withKeyboard.messageId = 7;
    withKeyboard.caption = "cap";
    withKeyboard.replyMarkup = singleButtonMarkup();
    ASSERT_TRUE(api.editMessageCaption(withKeyboard) == nullptr);
    EXPECT_EQ(httpClient.field("caption"), "cap");
    EXPECT_TRUE(httpClient.has("reply_markup"));

    cppgram::bot::EditMessageCaptionArgs withoutKeyboard;
    withoutKeyboard.chatId = std::int64_t { 42 };
    withoutKeyboard.messageId = 7;
    withoutKeyboard.caption = "plain";
    ASSERT_TRUE(api.editMessageCaption(withoutKeyboard) == nullptr);
    EXPECT_FALSE(httpClient.has("reply_markup"));
}

TEST(InlineKeyboard, EditMessageMediaSendsKeyboard) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    auto photo = std::make_shared<cppgram::bot::InputMediaPhoto>();
    photo->media = "https://example.com/p.jpg";
    auto media = std::make_shared<cppgram::bot::InputMedia>();
    media->value = photo;

    ASSERT_TRUE(api.editMessageMedia(media, std::int64_t { 42 }, 7, "", singleButtonMarkup(), "", {}) == nullptr);
    EXPECT_TRUE(httpClient.has("media"));
    EXPECT_EQ(nlohmann::json::parse(httpClient.field("reply_markup")).at("inline_keyboard")[0][0].at("url"),
              "https://example.com");
}

// ---- answerCallbackQuery

TEST(InlineKeyboard, AnswerCallbackQueryBodyOmitsDefaults) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    ASSERT_TRUE(api.answerCallbackQuery("cb-1", "", false, "", 0));
    EXPECT_EQ(httpClient.field("callback_query_id"), "cb-1");
    EXPECT_FALSE(httpClient.has("text"));
    EXPECT_FALSE(httpClient.has("show_alert"));
    EXPECT_FALSE(httpClient.has("url"));
    EXPECT_FALSE(httpClient.has("cache_time"));
}

TEST(InlineKeyboard, AnswerCallbackQuerySendsAlertTextAndCacheTime) {
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    cppgram::bot::AnswerCallbackQueryArgs args;
    args.callbackQueryId = "cb-2";
    args.text = "done";
    args.showAlert = true;
    args.cacheTime = 30;
    ASSERT_TRUE(api.answerCallbackQuery(args));
    EXPECT_EQ(httpClient.field("text"), "done");
    EXPECT_EQ(httpClient.field("show_alert"), "1");
    EXPECT_EQ(httpClient.field("cache_time"), "30");
}

// ---- CallbackQuery parsing

TEST(InlineKeyboard, CallbackQueryParsesInaccessibleMessage) {
    // Telegram sends date 0 for a message the bot can no longer access. It must not be parsed as a full Message.
    const auto query = nlohmann::json::parse(R"({
        "id": "1",
        "from": { "id": 9, "is_bot": false, "first_name": "u" },
        "chat_instance": "ci",
        "data": "pick:1",
        "message": { "chat": { "id": 42, "type": "private" }, "message_id": 7, "date": 0 }
    })")
                           .get<cppgram::bot::CallbackQuery>();

    EXPECT_EQ(query.data, "pick:1");
    ASSERT_TRUE(query.message);
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<cppgram::bot::InaccessibleMessage>>(query.message->value));
}

TEST(InlineKeyboard, CallbackQueryParsesAccessibleMessage) {
    const auto query = nlohmann::json::parse(R"({
        "id": "2",
        "from": { "id": 9, "is_bot": false, "first_name": "u" },
        "chat_instance": "ci",
        "message": { "message_id": 8, "date": 1700000000, "chat": { "id": 42, "type": "private" } }
    })")
                           .get<cppgram::bot::CallbackQuery>();

    ASSERT_TRUE(query.message);
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<cppgram::bot::Message>>(query.message->value));
    EXPECT_FALSE(query.data.has_value() && !query.data->empty());
}

TEST(InlineKeyboard, CallbackQueryRequiresIdFromAndChatInstance) {
    EXPECT_THROW(nlohmann::json::parse(R"({"from":{"id":9,"is_bot":false,"first_name":"u"},"chat_instance":"ci"})")
                     .get<cppgram::bot::CallbackQuery>(),
                 std::exception);
    EXPECT_THROW(nlohmann::json::parse(R"({"id":"1","from":{"id":9,"is_bot":false,"first_name":"u"}})")
                     .get<cppgram::bot::CallbackQuery>(),
                 std::exception);
}

// ---- Dispatch

TEST(InlineKeyboard, CallbackQueryReachesEveryListenerInOrder) {
    cppgram::bot::EventBroadcaster broadcaster;
    cppgram::bot::EventHandler handler(broadcaster);
    auto update = std::make_shared<cppgram::bot::Update>();
    update->callbackQuery = std::make_shared<cppgram::bot::CallbackQuery>();
    update->callbackQuery->data = "x";

    std::vector<int> order;
    broadcaster.onCallbackQuery([&](const auto& query) {
        EXPECT_EQ(query->data, "x");
        order.push_back(1);
    });
    broadcaster.onCallbackQuery([&](const auto&) {
        order.push_back(2);
    });
    handler.handleUpdate(update);

    EXPECT_EQ(order, (std::vector<int> { 1, 2 }));
}

TEST(InlineKeyboard, CallbackListenerExceptionPropagatesAndStopsLaterListeners) {
    // Current contract, pinned here so any change is deliberate: a throwing listener propagates out of handleUpdate
    // and later listeners for the same update are not called.
    cppgram::bot::EventBroadcaster broadcaster;
    cppgram::bot::EventHandler handler(broadcaster);
    auto update = std::make_shared<cppgram::bot::Update>();
    update->callbackQuery = std::make_shared<cppgram::bot::CallbackQuery>();

    int laterCalls = 0;
    broadcaster.onCallbackQuery([](const auto&) {
        throw std::runtime_error("listener failed");
    });
    broadcaster.onCallbackQuery([&](const auto&) {
        ++laterCalls;
    });

    EXPECT_THROW(handler.handleUpdate(update), std::runtime_error);
    EXPECT_EQ(laterCalls, 0);
}

TEST(InlineKeyboard, UpdateWithoutCallbackQueryDoesNotCallCallbackListeners) {
    cppgram::bot::EventBroadcaster broadcaster;
    cppgram::bot::EventHandler handler(broadcaster);
    auto update = std::make_shared<cppgram::bot::Update>();

    int calls = 0;
    broadcaster.onCallbackQuery([&](const auto&) {
        ++calls;
    });
    handler.handleUpdate(update);
    EXPECT_EQ(calls, 0);
}

// Same code as the callback and message examples in docs/INLINE_KEYBOARD.md.
TEST(InlineKeyboard, DocsExampleCallbackAndMessageHandling) {
    cppgram::bot::EventBroadcaster broadcaster;
    cppgram::bot::EventHandler handler(broadcaster);
    HttpClientMock httpClient;
    cppgram::bot::Api api("token", httpClient, "https://api.telegram.org");

    broadcaster.onCallbackQuery([&](const std::shared_ptr<cppgram::bot::CallbackQuery>& query) {
        if (query->data && *query->data == "vote:yes") {
            api.answerCallbackQuery(query->id, "Recorded", false, "", 0);
        }
    });

    auto update = std::make_shared<cppgram::bot::Update>();
    update->callbackQuery = std::make_shared<cppgram::bot::CallbackQuery>();
    update->callbackQuery->id = "cb-doc";
    update->callbackQuery->data = "vote:yes";
    handler.handleUpdate(update);
    EXPECT_EQ(httpClient.field("text"), "Recorded");

    auto inaccessible = std::make_shared<cppgram::bot::CallbackQuery>();
    inaccessible->message = std::make_shared<cppgram::bot::MaybeInaccessibleMessage>();
    inaccessible->message->value = std::make_shared<cppgram::bot::InaccessibleMessage>();
    if (inaccessible->message) {
        EXPECT_FALSE(std::holds_alternative<std::shared_ptr<cppgram::bot::Message>>(inaccessible->message->value));
    }
}
