// Offline conversion tests for inline and reply keyboards: public types to TL and back. No network. NOT RUN in this
// session. Round trips compare every field the public type carries. Unrepresentable TL input must throw
// errors::SerializationError, never be dropped.
#include <gtest/gtest.h>

#include <stdexcept>
#include <variant>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/schema/api.h"
#include "../../src/markup_detail.h"

using cppgram::ButtonStyle;
using cppgram::InlineButton;
using cppgram::InlineKeyboard;
using cppgram::InlineQueryPeer;
using cppgram::ReplyButtonKind;
using cppgram::ReplyKeyboard;
using cppgram::ReplyKeyboardButton;
namespace detail = cppgram::detail;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

InlineKeyboard round_trip_inline(const InlineKeyboard& kb) {
    auto markup = detail::to_tl_inline_markup(kb);
    auto back = detail::from_tl_inline_markup(*markup);
    EXPECT_TRUE(back.has_value());
    return back.value_or(InlineKeyboard{});
}

void expect_same_button(const InlineButton& a, const InlineButton& b) {
    EXPECT_EQ(a.kind, b.kind) << a.text;
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.value, b.value) << a.text;
    EXPECT_EQ(a.same_peer, b.same_peer) << a.text;
    EXPECT_EQ(a.requires_password, b.requires_password) << a.text;
    EXPECT_EQ(a.peer_types, b.peer_types) << a.text;
    EXPECT_EQ(a.fwd_text, b.fwd_text) << a.text;
    EXPECT_EQ(a.request_write_access, b.request_write_access) << a.text;
    EXPECT_EQ(a.user_id, b.user_id) << a.text;
    EXPECT_EQ(a.user_access_hash, b.user_access_hash) << a.text;
    EXPECT_EQ(a.style.has_value(), b.style.has_value()) << a.text;
    if (a.style && b.style) {
        EXPECT_EQ(a.style->primary, b.style->primary);
        EXPECT_EQ(a.style->danger, b.style->danger);
        EXPECT_EQ(a.style->success, b.style->success);
        EXPECT_EQ(a.style->icon, b.style->icon);
    }
}

}  // namespace

// Every inline button kind survives a round trip with all of its fields. Rows keep their order and boundaries.
TEST(KeyboardRoundTrip, EveryInlineButtonKindRoundTrips) {
    InlineButton callback = InlineButton::callback("cb", std::string("a\0b\xff", 4));
    callback.requires_password = true;
    InlineButton url = InlineButton::url("open", "https://example.org/x?y=1");
    InlineButton sw = InlineButton::switch_inline("share", "query text", true);
    sw.peer_types = {InlineQueryPeer::Pm, InlineQueryPeer::Megagroup, InlineQueryPeer::BotPm};
    InlineButton copy = InlineButton::copy_text("copy", "code 123");
    InlineButton login = InlineButton::login_url("login", "https://login.example", std::string("fwd"), true);
    login.user_id = 777;
    login.user_access_hash = 888;
    InlineButton web = InlineButton::web_app("app", "https://app.example");
    InlineButton buy = InlineButton::buy("pay");
    InlineButton game = InlineButton::game("play");
    InlineButton profile = InlineButton::user_profile("profile", 42, 4242);
    InlineButton disabled = InlineButton::disabled("off");
    InlineButton styled = InlineButton::url("styled", "https://styled.example");
    styled.style = ButtonStyle{true, false, true, std::int64_t{5555}};

    const InlineKeyboard kb = {
        {callback, url},
        {sw, copy, login},
        {web},
        {buy, game, profile, disabled, styled},
    };
    const InlineKeyboard back = round_trip_inline(kb);
    ASSERT_EQ(back.size(), kb.size());
    for (std::size_t r = 0; r < kb.size(); ++r) {
        ASSERT_EQ(back[r].size(), kb[r].size()) << "row " << r;
        for (std::size_t c = 0; c < kb[r].size(); ++c) expect_same_button(back[r][c], kb[r][c]);
    }
}

// A received login button carries its login_button_id. The sent form does not, so the id is 0 after a sent round trip.
TEST(KeyboardRoundTrip, ReceivedLoginButtonKeepsItsId) {
    auto received = std::make_shared<tl::KeyboardInlineButton>(tl::KeyboardInlineButton{ty::keyboardInlineButton{
        std::nullopt, "login",
        std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeUrlAuth{
            std::string("fwd"), "https://login.example", 19}})}});
    ty::replyInlineMarkup markup;
    ty::keyboardInlineButtonRow row;
    row.buttons.push_back(received);
    markup.rows.push_back(std::make_shared<tl::KeyboardInlineButtonRow>(tl::KeyboardInlineButtonRow{row}));
    tl::ReplyMarkup wrapped{markup};
    auto kb = detail::from_tl_inline_markup(wrapped);
    ASSERT_TRUE(kb.has_value());
    ASSERT_EQ(kb->size(), 1u);
    ASSERT_EQ((*kb)[0].size(), 1u);
    EXPECT_EQ((*kb)[0][0].kind, InlineButton::Kind::LoginUrl);
    EXPECT_EQ((*kb)[0][0].login_button_id, 19);
    EXPECT_EQ((*kb)[0][0].fwd_text, std::optional<std::string>("fwd"));
}

// An empty inline keyboard is a valid markup with no rows.
TEST(KeyboardRoundTrip, EmptyInlineKeyboardRoundTrips) {
    EXPECT_TRUE(round_trip_inline({}).empty());
    const InlineKeyboard one_empty_row = {{}};
    const InlineKeyboard back = round_trip_inline(one_empty_row);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_TRUE(back[0].empty());
}

// Every typed reply button kind round-trips with its fields. The flags of the keyboard survive too.
TEST(KeyboardRoundTrip, EveryReplyButtonKindRoundTrips) {
    ReplyKeyboard kb;
    kb.resize = false;
    kb.one_time = true;
    kb.selective = true;
    kb.persistent = true;
    kb.placeholder = "pick one";

    ReplyKeyboardButton text;
    text.text = "plain";
    ReplyKeyboardButton phone;
    phone.kind = ReplyButtonKind::RequestPhone;
    phone.text = "share phone";
    ReplyKeyboardButton geo;
    geo.kind = ReplyButtonKind::RequestGeoLocation;
    geo.text = "share location";
    ReplyKeyboardButton poll;
    poll.kind = ReplyButtonKind::RequestPoll;
    poll.text = "quiz";
    poll.poll_quiz = true;
    ReplyKeyboardButton user;
    user.kind = ReplyButtonKind::RequestUser;
    user.text = "pick user";
    user.request_id = 7;
    user.max_quantity = 3;
    user.bot = false;
    user.premium = true;
    ReplyKeyboardButton chat;
    chat.kind = ReplyButtonKind::RequestChat;
    chat.text = "pick chat";
    chat.request_id = 8;
    chat.chat_creator = true;
    chat.chat_bot_participant = true;
    chat.has_username = false;
    chat.forum = true;
    ReplyKeyboardButton channel;
    channel.kind = ReplyButtonKind::RequestChannel;
    channel.text = "pick channel";
    channel.request_id = 9;
    channel.has_username = true;
    ReplyKeyboardButton web;
    web.kind = ReplyButtonKind::WebApp;
    web.text = "open";
    web.url = "https://app.example";
    web.style = ButtonStyle{false, true, false, std::nullopt};

    kb.typed_rows = {{text, phone, geo}, {poll, user}, {chat, channel, web}};

    auto markup = detail::to_tl_reply_markup(kb);
    auto back = detail::from_tl_reply_markup(*markup);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->resize, false);
    EXPECT_EQ(back->one_time, true);
    EXPECT_EQ(back->selective, true);
    EXPECT_EQ(back->persistent, true);
    EXPECT_EQ(back->placeholder, "pick one");
    ASSERT_EQ(back->typed_rows.size(), 3u);
    const auto& out = back->typed_rows;
    ASSERT_EQ(out[0].size(), 3u);
    EXPECT_EQ(out[0][0].kind, ReplyButtonKind::Text);
    EXPECT_EQ(out[0][0].text, "plain");
    EXPECT_EQ(out[0][1].kind, ReplyButtonKind::RequestPhone);
    EXPECT_EQ(out[0][2].kind, ReplyButtonKind::RequestGeoLocation);
    EXPECT_EQ(out[1][0].kind, ReplyButtonKind::RequestPoll);
    EXPECT_EQ(out[1][0].poll_quiz, std::optional<bool>(true));
    EXPECT_EQ(out[1][1].kind, ReplyButtonKind::RequestUser);
    EXPECT_EQ(out[1][1].request_id, 7);
    EXPECT_EQ(out[1][1].max_quantity, 3);
    EXPECT_EQ(out[1][1].bot, std::optional<bool>(false));
    EXPECT_EQ(out[1][1].premium, std::optional<bool>(true));
    EXPECT_EQ(out[2][0].kind, ReplyButtonKind::RequestChat);
    EXPECT_TRUE(out[2][0].chat_creator);
    EXPECT_TRUE(out[2][0].chat_bot_participant);
    EXPECT_EQ(out[2][0].has_username, std::optional<bool>(false));
    EXPECT_EQ(out[2][0].forum, std::optional<bool>(true));
    EXPECT_EQ(out[2][1].kind, ReplyButtonKind::RequestChannel);
    EXPECT_EQ(out[2][1].has_username, std::optional<bool>(true));
    EXPECT_EQ(out[2][2].kind, ReplyButtonKind::WebApp);
    EXPECT_EQ(out[2][2].url, "https://app.example");
    ASSERT_TRUE(out[2][2].style.has_value());
    EXPECT_TRUE(out[2][2].style->danger);
}

// Plain string rows come back as typed Text rows, with the same labels in the same order.
TEST(KeyboardRoundTrip, StringRowsDecodeAsTextButtons) {
    ReplyKeyboard kb;
    kb.rows = {{"a", "b"}, {}, {"c"}};
    auto markup = detail::to_tl_reply_markup(kb);
    auto back = detail::from_tl_reply_markup(*markup);
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->typed_rows.size(), 3u);
    EXPECT_EQ(back->typed_rows[0].size(), 2u);
    EXPECT_EQ(back->typed_rows[0][1].text, "b");
    EXPECT_TRUE(back->typed_rows[1].empty());
    EXPECT_EQ(back->typed_rows[2][0].text, "c");
    for (const auto& row : back->typed_rows) {
        for (const auto& b : row) EXPECT_EQ(b.kind, ReplyButtonKind::Text);
    }
}

// Force reply and hide are their own forms and round-trip.
TEST(KeyboardRoundTrip, ForceReplyAndHideRoundTrip) {
    ReplyKeyboard force;
    force.force_reply = true;
    force.one_time = true;
    force.placeholder = "type here";
    auto fr = detail::from_tl_reply_markup(*detail::to_tl_reply_markup(force));
    ASSERT_TRUE(fr.has_value());
    EXPECT_TRUE(fr->force_reply);
    EXPECT_TRUE(fr->one_time);
    EXPECT_EQ(fr->placeholder, "type here");
    EXPECT_FALSE(fr->hide);

    ReplyKeyboard hide;
    hide.hide = true;
    hide.selective = true;
    auto hd = detail::from_tl_reply_markup(*detail::to_tl_reply_markup(hide));
    ASSERT_TRUE(hd.has_value());
    EXPECT_TRUE(hd->hide);
    EXPECT_TRUE(hd->selective);
}

// Every decoder answers nullopt for the other family of markup, so callers can route the value.
TEST(KeyboardRoundTrip, MarkupFamiliesAreSeparated) {
    const InlineKeyboard inline_kb = {{InlineButton::url("x", "https://x")}};
    auto inline_markup = detail::to_tl_inline_markup(inline_kb);
    EXPECT_FALSE(detail::from_tl_reply_markup(*inline_markup).has_value());
    ReplyKeyboard reply_kb;
    reply_kb.rows = {{"x"}};
    auto reply_markup = detail::to_tl_reply_markup(reply_kb);
    EXPECT_FALSE(detail::from_tl_inline_markup(*reply_markup).has_value());
}

// Input that the public types cannot represent throws a typed error. Nothing is dropped.
TEST(KeyboardRoundTrip, UnrepresentableRequestPeerThrowsTypedError) {
    ty::inputButtonTypeRequestPeer req;
    req.button_id = 1;
    req.max_quantity = 1;
    ty::requestPeerTypeChat chat;
    chat.user_admin_rights = std::make_shared<tl::ChatAdminRights>(tl::ChatAdminRights{ty::chatAdminRights{}});
    req.peer_type = std::make_shared<tl::RequestPeerType>(tl::RequestPeerType{chat});
    ty::keyboardButton btn;
    btn.text = "pick";
    btn.type = std::make_shared<tl::ButtonType>(tl::ButtonType{req});
    ty::keyboardButtonRow row;
    row.buttons.push_back(std::make_shared<tl::KeyboardButton>(tl::KeyboardButton{btn}));
    ty::replyKeyboardMarkup markup;
    markup.rows.push_back(std::make_shared<tl::KeyboardButtonRow>(tl::KeyboardButtonRow{row}));
    tl::ReplyMarkup wrapped{markup};
    EXPECT_THROW(detail::from_tl_reply_markup(wrapped), cppgram::errors::SerializationError);
}

// Two ways to give a reply keyboard, and an invalid row count on a peer request, throw before anything is built.
TEST(KeyboardRoundTrip, AmbiguousReplyKeyboardThrows) {
    ReplyKeyboard both;
    both.rows = {{"a"}};
    both.typed_rows = {{ReplyKeyboardButton{}}};
    EXPECT_THROW(detail::to_tl_reply_markup(both), std::invalid_argument);

    ReplyKeyboard force_and_hide;
    force_and_hide.force_reply = true;
    force_and_hide.hide = true;
    EXPECT_THROW(detail::to_tl_reply_markup(force_and_hide), std::invalid_argument);

    ReplyKeyboard too_many;
    ReplyKeyboardButton user;
    user.kind = ReplyButtonKind::RequestUser;
    user.max_quantity = 11;
    too_many.typed_rows = {{user}};
    EXPECT_THROW(detail::to_tl_reply_markup(too_many), std::invalid_argument);
}

// Inline button fields that a kind does not use must not change its sent form.
TEST(KeyboardRoundTrip, UnusedFieldsDoNotLeakIntoOtherKinds) {
    InlineButton plain = InlineButton::copy_text("c", "text");
    plain.requires_password = true;  // not a callback field
    plain.peer_types = {InlineQueryPeer::Pm};  // not a switch-inline field
    auto tl_button = detail::to_tl_inline_button(plain);
    const auto* kb = std::get_if<ty::keyboardInlineButton>(&tl_button->value);
    ASSERT_NE(kb, nullptr);
    ASSERT_TRUE(kb->type);
    const auto* copy = std::get_if<ty::inlineButtonTypeCopy>(&kb->type->value);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->copy_text, "text");
}
