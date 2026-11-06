// Outbound validation for InlineKeyboardButton and InlineKeyboardMarkup. Offline only.

#include <gtest/gtest.h>

#include "cppgram/bot/InlineKeyboardValidation.h"
#include "cppgram/bot/Types.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using cppgram::bot::InlineKeyboardButton;
using cppgram::bot::InlineKeyboardMarkup;
using cppgram::bot::validateInlineKeyboardButton;
using cppgram::bot::validateInlineKeyboardMarkup;

std::shared_ptr<InlineKeyboardButton> buttonWithLabel() {
    auto button = std::make_shared<InlineKeyboardButton>();
    button->text = "label";
    return button;
}

} // namespace

// ---- Exactly one action field

TEST(InlineKeyboardValidation, EachSingleActionIsAccepted) {
    {
        auto b = buttonWithLabel();
        b->url = "https://example.com";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "url";
    }
    {
        auto b = buttonWithLabel();
        b->callbackData = "pick:1";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "callback_data";
    }
    {
        auto b = buttonWithLabel();
        b->webApp = std::make_shared<cppgram::bot::WebAppInfo>();
        b->webApp->url = "https://example.com/app";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "web_app";
    }
    {
        auto b = buttonWithLabel();
        b->loginUrl = std::make_shared<cppgram::bot::LoginUrl>();
        b->loginUrl->url = "https://example.com/login";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "login_url";
    }
    {
        auto b = buttonWithLabel();
        b->switchInlineQuery = "";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "switch_inline_query (empty string is still set)";
    }
    {
        auto b = buttonWithLabel();
        b->switchInlineQueryCurrentChat = "q";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "switch_inline_query_current_chat";
    }
    {
        auto b = buttonWithLabel();
        b->switchInlineQueryChosenChat = std::make_shared<cppgram::bot::SwitchInlineQueryChosenChat>();
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "switch_inline_query_chosen_chat";
    }
    {
        auto b = buttonWithLabel();
        b->copyText = std::make_shared<cppgram::bot::CopyTextButton>();
        b->copyText->text = "copy";
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "copy_text";
    }
    {
        auto b = buttonWithLabel();
        b->callbackGame = std::make_shared<cppgram::bot::CallbackGame>();
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "callback_game";
    }
    {
        auto b = buttonWithLabel();
        b->pay = true;
        EXPECT_FALSE(validateInlineKeyboardButton(*b)) << "pay";
    }
}

TEST(InlineKeyboardValidation, ButtonWithoutActionIsRejected) {
    const auto error = validateInlineKeyboardButton(*buttonWithLabel());
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("exactly one"), std::string::npos) << *error;
}

TEST(InlineKeyboardValidation, ButtonWithTwoActionsIsRejected) {
    auto b = buttonWithLabel();
    b->url = "https://example.com";
    b->callbackData = "pick:1";
    const auto error = validateInlineKeyboardButton(*b);
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("exactly one"), std::string::npos) << *error;
}

TEST(InlineKeyboardValidation, EveryActionPairIsRejected) {
    // Ten action fields. Setting any two of them must fail.
    const std::vector<std::function<void(InlineKeyboardButton&)>> setters = {
        [](InlineKeyboardButton& b) {
            b.url = "https://example.com";
        },
        [](InlineKeyboardButton& b) {
            b.callbackData = "d";
        },
        [](InlineKeyboardButton& b) {
            b.webApp = std::make_shared<cppgram::bot::WebAppInfo>();
        },
        [](InlineKeyboardButton& b) {
            b.loginUrl = std::make_shared<cppgram::bot::LoginUrl>();
        },
        [](InlineKeyboardButton& b) {
            b.switchInlineQuery = "";
        },
        [](InlineKeyboardButton& b) {
            b.switchInlineQueryCurrentChat = "";
        },
        [](InlineKeyboardButton& b) {
            b.switchInlineQueryChosenChat = std::make_shared<cppgram::bot::SwitchInlineQueryChosenChat>();
        },
        [](InlineKeyboardButton& b) {
            b.copyText = std::make_shared<cppgram::bot::CopyTextButton>();
        },
        [](InlineKeyboardButton& b) {
            b.callbackGame = std::make_shared<cppgram::bot::CallbackGame>();
        },
        [](InlineKeyboardButton& b) {
            b.pay = true;
        },
    };
    for (std::size_t i = 0; i < setters.size(); ++i) {
        for (std::size_t j = i + 1; j < setters.size(); ++j) {
            auto b = buttonWithLabel();
            setters[i](*b);
            setters[j](*b);
            EXPECT_TRUE(validateInlineKeyboardButton(*b)) << "pair " << i << "," << j;
        }
    }
}

TEST(InlineKeyboardValidation, DisabledIsNotCountedAsAction) {
    // disabled marks a button that does nothing. It does not replace the action field, so url + disabled is valid
    // and disabled alone is still missing an action.
    auto withUrl = buttonWithLabel();
    withUrl->url = "https://example.com";
    withUrl->disabled = std::make_shared<cppgram::bot::DisabledButton>();
    EXPECT_FALSE(validateInlineKeyboardButton(*withUrl));

    auto alone = buttonWithLabel();
    alone->disabled = std::make_shared<cppgram::bot::DisabledButton>();
    EXPECT_TRUE(validateInlineKeyboardButton(*alone));
}

// ---- callback_data length: 1 to 64 bytes

TEST(InlineKeyboardValidation, CallbackDataLengthBounds) {
    auto empty = buttonWithLabel();
    empty->callbackData = "";
    EXPECT_TRUE(validateInlineKeyboardButton(*empty));

    auto max = buttonWithLabel();
    max->callbackData = std::string(64, 'a');
    EXPECT_FALSE(validateInlineKeyboardButton(*max));

    auto tooLong = buttonWithLabel();
    tooLong->callbackData = std::string(65, 'a');
    const auto error = validateInlineKeyboardButton(*tooLong);
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("64"), std::string::npos) << *error;
}

// ---- Markup

TEST(InlineKeyboardValidation, EmptyMarkupIsValidForRemoval) {
    InlineKeyboardMarkup markup;
    EXPECT_FALSE(validateInlineKeyboardMarkup(markup));
}

TEST(InlineKeyboardValidation, MultiRowMultiButtonMarkupIsValid) {
    auto a = buttonWithLabel();
    a->url = "https://example.com/a";
    auto b = buttonWithLabel();
    b->callbackData = "b";
    auto c = buttonWithLabel();
    c->copyText = std::make_shared<cppgram::bot::CopyTextButton>();
    c->copyText->text = "c";

    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = { { a, b }, { c } };
    EXPECT_FALSE(validateInlineKeyboardMarkup(markup));
}

TEST(InlineKeyboardValidation, LargeValidMarkupIsValid) {
    InlineKeyboardMarkup markup;
    for (int row = 0; row < 100; ++row) {
        std::vector<std::shared_ptr<InlineKeyboardButton>> buttons;
        for (int col = 0; col < 8; ++col) {
            auto b = buttonWithLabel();
            b->callbackData = "r" + std::to_string(row) + "c" + std::to_string(col);
            buttons.push_back(std::move(b));
        }
        markup.inlineKeyboard.push_back(std::move(buttons));
    }
    EXPECT_FALSE(validateInlineKeyboardMarkup(markup));
}

TEST(InlineKeyboardValidation, NullButtonIsRejectedWithPosition) {
    auto ok = buttonWithLabel();
    ok->url = "https://example.com";

    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = { { ok }, { ok, nullptr } };
    const auto error = validateInlineKeyboardMarkup(markup);
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("row 1"), std::string::npos) << *error;
    EXPECT_NE(error->find("column 1"), std::string::npos) << *error;
}

TEST(InlineKeyboardValidation, InvalidButtonInsideMarkupIsRejectedWithPosition) {
    auto ok = buttonWithLabel();
    ok->url = "https://example.com";
    auto bad = buttonWithLabel();

    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = { { ok, bad } };
    const auto error = validateInlineKeyboardMarkup(markup);
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("row 0"), std::string::npos) << *error;
    EXPECT_NE(error->find("column 1"), std::string::npos) << *error;
}

TEST(InlineKeyboardValidation, EmptyRowIsAcceptedByTheLibrary) {
    // Telegram's own handling of an empty row is not verified here. The library does not reject it.
    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = { {} };
    EXPECT_FALSE(validateInlineKeyboardMarkup(markup));
}

// Same code as the example in docs/INLINE_KEYBOARD.md. If this test changes, update the doc.
TEST(InlineKeyboardValidation, DocsExampleBuildsValidKeyboard) {
    auto open = std::make_shared<InlineKeyboardButton>();
    open->text = "Open";
    open->url = "https://example.com";

    auto yes = std::make_shared<InlineKeyboardButton>();
    yes->text = "Yes";
    yes->callbackData = "vote:yes";

    auto copy = std::make_shared<InlineKeyboardButton>();
    copy->text = "Copy code";
    copy->copyText = std::make_shared<cppgram::bot::CopyTextButton>();
    copy->copyText->text = "ABC-123";

    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = { { open }, { yes, copy } };

    EXPECT_FALSE(validateInlineKeyboardMarkup(markup));
    const auto json = nlohmann::json(markup);
    EXPECT_EQ(json.at("inline_keyboard")[1][1].at("copy_text").at("text"), "ABC-123");
}
