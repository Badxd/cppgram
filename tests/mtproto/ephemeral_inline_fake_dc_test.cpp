// Request-shape and response-decoding tests for ephemeral.sendMessage, ephemeral.editMessage and
// messages.getInlineBotResults, against scripted fake DCs. Offline only. NOT RUN in this session: the owner runs the
// suite. Each request test parses the flags word and the fields that the flags announce, so a wrong flag bit or a
// missing field fails the test instead of passing silently.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::EphemeralMessageOptions;
using cppgram::MediaInfo;
using cppgram::MediaKind;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::RichMessageInput;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;
using cppgram::mtproto::Bytes;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

namespace {

// Flag bits of ephemeral.sendMessage, from schema/main_api.tl line "ephemeral.sendMessage#ba8d5f35".
constexpr std::uint32_t kSendQueryId = 1u << 0;
constexpr std::uint32_t kSendEntities = 1u << 1;
constexpr std::uint32_t kSendMedia = 1u << 2;
constexpr std::uint32_t kSendReplyMarkup = 1u << 3;
constexpr std::uint32_t kSendRichMessage = 1u << 4;
constexpr std::uint32_t kSendReplyTo = 1u << 5;
constexpr std::uint32_t kSendInvertMedia = 1u << 6;
constexpr std::uint32_t kSendWelcome = 1u << 7;
constexpr std::uint32_t kSendPeer = 1u << 8;
constexpr std::uint32_t kSendAnchor = 1u << 9;
constexpr std::uint32_t kSendNoforwards = 1u << 10;

// Flag bits of ephemeral.editMessage, from "ephemeral.editMessage#cf9c725b".
constexpr std::uint32_t kEditMessage = 1u << 0;
constexpr std::uint32_t kEditReplyMarkup = 1u << 2;
constexpr std::uint32_t kEditMedia = 1u << 3;
constexpr std::uint32_t kEditPeer = 1u << 7;

std::string temp_name(const char* tag, const char* ext) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-ephinline-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter++) + ext))
        .string();
}

ClientOptions options_for(FakeDc& dc, const std::string& session, std::vector<cppgram::mtproto::RsaPublicKey> keys) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = std::move(keys);
    o.session_path = session;
    o.connect_timeout_ms = 2000;
    o.verify_cdn_hashes = true;
    return o;
}

PeerRef user_peer(std::int64_t id) { return PeerRef{PeerKind::User, id, 99, true}; }

MediaInfo cached_photo() {
    MediaInfo m;
    m.kind = MediaKind::Photo;
    m.id = 4001;
    m.access_hash = 4002;
    m.file_reference = Bytes{1, 2, 3};
    return m;
}

MediaInfo cached_document() {
    MediaInfo m;
    m.kind = MediaKind::Document;
    m.id = 5001;
    m.access_hash = 5002;
    m.file_reference = Bytes{4, 5};
    return m;
}

}  // namespace

// ephemeral.sendMessage with a peer, welcome, anchor, noforwards, invert_media, a cached photo and an inline keyboard.
// The handler parses every field the flags announce, in schema order, and the test checks each flag bit.
TEST(EphemeralInlineFakeDc, EphemeralSendAnnouncesEveryFieldItCarries) {
    FakeDc dc;
    std::uint32_t flags = 0;
    bool has_peer = false;
    bool has_media_photo = false;
    bool has_inline_markup = false;
    bool announced = false;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::ephemeral_sendMessage::kId) return false;
        announced = true;
        flags = args.read_uint32();
        if (flags & kSendPeer) {
            (void)cppgram::mtproto::tl::InputPeer::read(args);
            has_peer = true;
        }
        (void)cppgram::mtproto::tl::InputUser::read(args);  // receiver_id, always present
        if (flags & kSendQueryId) (void)args.read_int64();
        (void)args.read_string();  // message, empty for a media-only send
        if (flags & kSendEntities) throw std::logic_error("this test sends no entities");
        if (flags & kSendMedia) {
            const auto media = cppgram::mtproto::tl::InputMedia::read(args);
            has_media_photo = std::holds_alternative<ty::inputMediaPhoto>(media.value);
        }
        if (flags & kSendReplyMarkup) {
            const auto markup = cppgram::mtproto::tl::ReplyMarkup::read(args);
            has_inline_markup = std::holds_alternative<ty::replyInlineMarkup>(markup.value);
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = temp_name("send-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    EphemeralMessageOptions options;
    options.peer = user_peer(555);
    options.welcome = true;
    options.anchor = true;
    options.noforwards = true;
    options.invert_media = true;
    options.media = cached_photo();
    options.inline_markup = cppgram::InlineKeyboard{{cppgram::InlineButton::url("open", "https://example.org")}};
    const bool ok = client.ephemeral_send_message(user_peer(777), "", options);
    client.disconnect();
    std::filesystem::remove(session);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(announced) << "the handler must see ephemeral.sendMessage";
    EXPECT_NE(flags & kSendPeer, 0u);
    EXPECT_NE(flags & kSendAnchor, 0u);
    EXPECT_NE(flags & kSendWelcome, 0u);
    EXPECT_NE(flags & kSendNoforwards, 0u);
    EXPECT_NE(flags & kSendInvertMedia, 0u);
    EXPECT_NE(flags & kSendMedia, 0u);
    EXPECT_NE(flags & kSendReplyMarkup, 0u);
    EXPECT_EQ(flags & kSendRichMessage, 0u) << "no rich message was requested";
    EXPECT_EQ(flags & kSendReplyTo, 0u);
    EXPECT_TRUE(has_peer);
    EXPECT_TRUE(has_media_photo) << "a cached photo must be sent as inputMediaPhoto";
    EXPECT_TRUE(has_inline_markup) << "the inline keyboard must be sent as replyInlineMarkup";
}

// Same method with a rich HTML body and one photo file, and no text. The file id must reach inputRichFilePhoto.
TEST(EphemeralInlineFakeDc, EphemeralSendRichBodyCarriesHtmlAndPhotoFile) {
    FakeDc dc;
    std::uint32_t flags = 0;
    std::string rich_html;
    std::string photo_id;
    std::string message_text = "unset";
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::ephemeral_sendMessage::kId) return false;
        flags = args.read_uint32();
        (void)cppgram::mtproto::tl::InputUser::read(args);
        if (flags & kSendQueryId) (void)args.read_int64();
        message_text = args.read_string();
        if (flags & kSendRichMessage) {
            const auto rich = cppgram::mtproto::tl::InputRichMessage::read(args);
            if (const auto* html = std::get_if<ty::inputRichMessageHTML>(&rich.value)) {
                rich_html = html->html;
                if (html->files && !html->files->empty()) {
                    if (const auto* file = std::get_if<ty::inputRichFilePhoto>(&(*html->files)[0]->value)) {
                        photo_id = file->id;
                    }
                }
            }
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = temp_name("rich-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    EphemeralMessageOptions options;
    RichMessageInput rich;
    rich.format = RichMessageInput::Format::Html;
    rich.body = "<p>hello</p><img src=\"photo\">";
    rich.files.push_back(cppgram::RichMessageFileInfo{"photo", cached_photo()});
    options.rich_message = rich;
    const bool ok = client.ephemeral_send_message(user_peer(777), "", options);
    client.disconnect();
    std::filesystem::remove(session);
    EXPECT_TRUE(ok);
    EXPECT_NE(flags & kSendRichMessage, 0u);
    EXPECT_EQ(flags & kSendMedia, 0u);
    EXPECT_TRUE(message_text.empty()) << "a rich message sends an empty message string";
    EXPECT_EQ(rich_html, "<p>hello</p><img src=\"photo\">");
    EXPECT_EQ(photo_id, "photo");
}

// ephemeral.editMessage with media and an inline keyboard and no text: the message flag must stay clear and the media
// and markup flags must be set.
TEST(EphemeralInlineFakeDc, EphemeralEditSendsMediaAndMarkupWithoutMessageFlag) {
    FakeDc dc;
    std::uint32_t flags = 0;
    bool media_doc = false;
    bool markup_reply = false;
    int edit_calls = 0;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::ephemeral_editMessage::kId) return false;
        ++edit_calls;
        flags = args.read_uint32();
        if (flags & kEditPeer) (void)cppgram::mtproto::tl::InputPeer::read(args);
        (void)cppgram::mtproto::tl::InputUser::read(args);
        (void)args.read_int32();  // id
        if (flags & kEditMessage) (void)args.read_string();
        if (flags & kEditMedia) {
            const auto media = cppgram::mtproto::tl::InputMedia::read(args);
            media_doc = std::holds_alternative<ty::inputMediaDocument>(media.value);
        }
        if (flags & kEditReplyMarkup) {
            const auto markup = cppgram::mtproto::tl::ReplyMarkup::read(args);
            markup_reply = std::holds_alternative<ty::replyInlineMarkup>(markup.value);
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = temp_name("edit-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    EphemeralMessageOptions options;
    options.media = cached_document();
    options.inline_markup = cppgram::InlineKeyboard{{cppgram::InlineButton::callback("go", "cb")}};
    const bool ok = client.ephemeral_edit_message(user_peer(777), 12, "", options);
    client.disconnect();
    std::filesystem::remove(session);
    EXPECT_TRUE(ok);
    EXPECT_EQ(edit_calls, 1);
    EXPECT_EQ(flags & kEditMessage, 0u) << "an empty text must not set the message flag";
    EXPECT_NE(flags & kEditMedia, 0u);
    EXPECT_NE(flags & kEditReplyMarkup, 0u);
    EXPECT_TRUE(media_doc) << "a cached document must be sent as inputMediaDocument";
    EXPECT_TRUE(markup_reply);
}

// Options that only apply to sendMessage, and invalid combinations, must be rejected before any request is sent.
TEST(EphemeralInlineFakeDc, EphemeralRejectsInvalidOptionsBeforeRpc) {
    FakeDc dc;
    int ephemeral_calls = 0;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id == fn::ephemeral_sendMessage::kId || id == fn::ephemeral_editMessage::kId) {
            ++ephemeral_calls;
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });

    const std::string session = temp_name("reject-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();

    EphemeralMessageOptions anchor_on_edit;
    anchor_on_edit.anchor = true;
    EXPECT_THROW(client.ephemeral_edit_message(user_peer(777), 12, "text", anchor_on_edit), std::invalid_argument);

    EphemeralMessageOptions noforwards_on_edit;
    noforwards_on_edit.noforwards = true;
    EXPECT_THROW(client.ephemeral_edit_message(user_peer(777), 12, "text", noforwards_on_edit), std::invalid_argument);

    EphemeralMessageOptions reply_on_edit;
    reply_on_edit.reply_to_msg_id = 3;
    EXPECT_THROW(client.ephemeral_edit_message(user_peer(777), 12, "text", reply_on_edit), std::invalid_argument);

    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "", EphemeralMessageOptions{}), std::invalid_argument)
        << "no text, media or rich message";

    EphemeralMessageOptions both_markups;
    both_markups.inline_markup = cppgram::InlineKeyboard{{cppgram::InlineButton::callback("a", "b")}};
    both_markups.reply_markup = cppgram::ReplyKeyboard{{{"yes", "no"}}};
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "text", both_markups), std::invalid_argument);

    EphemeralMessageOptions rich_with_text;
    RichMessageInput rich;
    rich.body = "<p>x</p>";
    rich_with_text.rich_message = rich;
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "text", rich_with_text), std::invalid_argument);

    EphemeralMessageOptions bad_media;
    MediaInfo none;
    bad_media.media = none;
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "text", bad_media), std::invalid_argument);

    EXPECT_THROW(client.ephemeral_edit_message(user_peer(777), 0, "text", EphemeralMessageOptions{}),
                 std::invalid_argument);

    client.disconnect();
    std::filesystem::remove(session);
    EXPECT_EQ(ephemeral_calls, 0) << "every invalid combination must fail before the RPC is sent";
}

// messages.getInlineBotResults returns one result per message kind. Each result must decode into its public fields,
// including the inline keyboard. Rich results are decoded as kind Rich with their keyboard only.
TEST(EphemeralInlineFakeDc, InlineBotResultsDecodesEveryMessageKind) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getInlineBotResults::kId) return false;

        ty::replyInlineMarkup markup;
        ty::keyboardInlineButtonRow row;
        ty::keyboardInlineButton button;
        button.text = "go";
        ty::inlineButtonTypeCallback cb;
        cb.data = Bytes{'c', 'b'};
        button.type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{cb});
        row.buttons.push_back(std::make_shared<tl::KeyboardInlineButton>(tl::KeyboardInlineButton{button}));
        markup.rows.push_back(std::make_shared<tl::KeyboardInlineButtonRow>(tl::KeyboardInlineButtonRow{row}));
        auto reply = std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{markup});

        auto geo_point = [](double lat, double lon) {
            ty::geoPoint g;
            g.lat = lat;
            g.long_ = lon;
            return std::make_shared<tl::GeoPoint>(tl::GeoPoint{g});
        };

        auto make_result = [](std::string rid, std::string rtype, tl::BotInlineMessage message) {
            ty::botInlineResult r;
            r.id = std::move(rid);
            r.type = std::move(rtype);
            r.send_message = std::make_shared<tl::BotInlineMessage>(std::move(message));
            return std::make_shared<tl::BotInlineResult>(tl::BotInlineResult{r});
        };

        std::vector<std::shared_ptr<tl::BotInlineResult>> results;

        ty::botInlineMessageText text;
        text.message = "hi";
        text.no_webpage = true;
        text.reply_markup = reply;
        results.push_back(make_result("text", "article", tl::BotInlineMessage{text}));

        ty::botInlineMessageMediaGeo g;
        g.geo = geo_point(1.5, 2.5);
        results.push_back(make_result("geo", "geo", tl::BotInlineMessage{g}));

        ty::botInlineMessageMediaVenue venue;
        venue.geo = geo_point(3.25, 4.75);
        venue.title = "cafe";
        venue.address = "1 main st";
        venue.provider = "foursquare";
        venue.venue_id = "v1";
        venue.venue_type = "coffee";
        results.push_back(make_result("venue", "venue", tl::BotInlineMessage{venue}));

        ty::botInlineMessageMediaContact contact;
        contact.phone_number = "+100";
        contact.first_name = "ann";
        contact.last_name = "lee";
        contact.vcard = "BEGIN:VCARD";
        results.push_back(make_result("contact", "contact", tl::BotInlineMessage{contact}));

        ty::botInlineMessageMediaInvoice invoice;
        invoice.title = "bill";
        invoice.description = "one item";
        invoice.currency = "USD";
        invoice.total_amount = 1234;
        invoice.test = true;
        invoice.shipping_address_requested = true;
        results.push_back(make_result("invoice", "invoice", tl::BotInlineMessage{invoice}));

        ty::botInlineMessageMediaWebPage web;
        web.message = "see this";
        web.url = "https://example.org/page";
        results.push_back(make_result("web", "article", tl::BotInlineMessage{web}));

        ty::botInlineMessageWidget widget;
        widget.width = 10;
        widget.height = 20;
        widget.payload = Bytes{'x', 'y', 'z'};
        // botInlineMessageWidget ties public_data and public_data_hash to one flag bit: both or neither.
        widget.public_data = std::string("pub");
        widget.public_data_hash = 77;
        widget.chat_data = std::string("chat");
        widget.message = "widget";
        results.push_back(make_result("widget", "widget", tl::BotInlineMessage{widget}));

        ty::messages_botResults response;
        response.query_id = 88;
        response.cache_time = 30;
        response.results = std::move(results);
        response.serialize(out);
        return true;
    });

    const std::string session = temp_name("inline-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();
    std::filesystem::remove(session);

    EXPECT_EQ(found.query_id, 88);
    EXPECT_EQ(found.cache_time, 30);
    ASSERT_EQ(found.results.size(), 7u);

    const auto& text = found.results[0];
    EXPECT_EQ(text.message_kind, cppgram::InlineMessageKind::Text);
    EXPECT_EQ(text.message_text, "hi");
    EXPECT_TRUE(text.no_webpage);
    ASSERT_TRUE(text.inline_markup.has_value());
    ASSERT_EQ(text.inline_markup->size(), 1u);
    ASSERT_EQ((*text.inline_markup)[0].size(), 1u);
    EXPECT_EQ((*text.inline_markup)[0][0].text, "go");
    EXPECT_EQ((*text.inline_markup)[0][0].kind, cppgram::InlineButton::Kind::Callback);
    EXPECT_EQ((*text.inline_markup)[0][0].value, "cb");

    const auto& geo = found.results[1];
    EXPECT_EQ(geo.message_kind, cppgram::InlineMessageKind::Geo);
    EXPECT_DOUBLE_EQ(geo.latitude, 1.5);
    EXPECT_DOUBLE_EQ(geo.longitude, 2.5);

    const auto& venue = found.results[2];
    EXPECT_EQ(venue.message_kind, cppgram::InlineMessageKind::Venue);
    EXPECT_DOUBLE_EQ(venue.latitude, 3.25);
    EXPECT_EQ(venue.venue_title, "cafe");
    EXPECT_EQ(venue.address, "1 main st");
    EXPECT_EQ(venue.provider, "foursquare");
    EXPECT_EQ(venue.venue_id, "v1");
    EXPECT_EQ(venue.venue_type, "coffee");

    const auto& contact = found.results[3];
    EXPECT_EQ(contact.message_kind, cppgram::InlineMessageKind::Contact);
    EXPECT_EQ(contact.phone_number, "+100");
    EXPECT_EQ(contact.first_name, "ann");
    EXPECT_EQ(contact.last_name, "lee");
    EXPECT_EQ(contact.vcard, "BEGIN:VCARD");

    const auto& invoice_result = found.results[4];
    EXPECT_EQ(invoice_result.message_kind, cppgram::InlineMessageKind::Invoice);
    EXPECT_EQ(invoice_result.invoice_title, "bill");
    EXPECT_EQ(invoice_result.invoice_description, "one item");
    EXPECT_EQ(invoice_result.currency, "USD");
    EXPECT_EQ(invoice_result.total_amount, 1234);
    EXPECT_TRUE(invoice_result.invoice_test);
    EXPECT_TRUE(invoice_result.shipping_address_requested);

    const auto& web_result = found.results[5];
    EXPECT_EQ(web_result.message_kind, cppgram::InlineMessageKind::WebPage);
    EXPECT_EQ(web_result.message_text, "see this");
    EXPECT_EQ(web_result.web_page_url, "https://example.org/page");

    const auto& widget_result = found.results[6];
    EXPECT_EQ(widget_result.message_kind, cppgram::InlineMessageKind::Widget);
    EXPECT_EQ(widget_result.widget_width, 10);
    EXPECT_EQ(widget_result.widget_height, 20);
    EXPECT_EQ(widget_result.widget_payload, (std::vector<std::uint8_t>{'x', 'y', 'z'}));
    ASSERT_TRUE(widget_result.public_data.has_value());
    EXPECT_EQ(*widget_result.public_data, "pub");
    ASSERT_TRUE(widget_result.public_data_hash.has_value());
    EXPECT_EQ(*widget_result.public_data_hash, 77);
    ASSERT_TRUE(widget_result.chat_data.has_value());
    EXPECT_EQ(*widget_result.chat_data, "chat");
    EXPECT_EQ(widget_result.message_text, "widget");
}
