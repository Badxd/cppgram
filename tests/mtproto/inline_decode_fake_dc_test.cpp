// Decoding of messages.getInlineBotResults results that carry web documents, rich bodies, invoice photos, registered
// widgets and reply keyboards. Scripted fake DC, offline only. NOT RUN in this session. Each test builds the TL result
// the fake DC returns and checks the public fields the client decodes from it.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::InlineMessageKind;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::ReplyButtonKind;
using cppgram::testing::FakeDc;
using cppgram::mtproto::Bytes;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

using ResultList = std::vector<std::shared_ptr<tl::BotInlineResult>>;

std::string temp_dir_name(const char* tag) {
    static std::atomic<int> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cppgram-inldec-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
                std::to_string(counter++));
    std::filesystem::create_directories(dir);
    return dir.string();
}

ClientOptions options_for(FakeDc& dc, const std::string& session) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = session;
    o.connect_timeout_ms = 2000;
    o.verify_cdn_hashes = true;
    return o;
}

PeerRef user_peer(std::int64_t id) { return PeerRef{PeerKind::User, id, 99, true}; }

std::shared_ptr<tl::BotInlineResult> wrap(const ty::botInlineResult& r) {
    return std::make_shared<tl::BotInlineResult>(tl::BotInlineResult{r});
}

ty::botInlineResult make_result(std::string id, std::string type, tl::BotInlineMessage message) {
    ty::botInlineResult r;
    r.id = std::move(id);
    r.type = std::move(type);
    r.send_message = std::make_shared<tl::BotInlineMessage>(std::move(message));
    return r;
}

std::shared_ptr<tl::WebDocument> web_doc(const ty::webDocument& d) {
    return std::make_shared<tl::WebDocument>(tl::WebDocument{d});
}

std::shared_ptr<tl::WebDocument> web_doc_no_proxy(const ty::webDocumentNoProxy& d) {
    return std::make_shared<tl::WebDocument>(tl::WebDocument{d});
}

std::shared_ptr<tl::DocumentAttribute> attr(tl::DocumentAttribute value) {
    return std::make_shared<tl::DocumentAttribute>(std::move(value));
}

// Script the fake DC to answer messages.getInlineBotResults with these results.
void answer_with(FakeDc& dc, ResultList results) {
    dc.set_handler([results](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getInlineBotResults::kId) return false;
        ty::messages_botResults response;
        response.query_id = 1;
        response.cache_time = 0;
        response.results = results;
        response.serialize(out);
        return true;
    });
}

}  // namespace

// Thumbnail and content web documents decode with their URL, MIME type, size, access hash and attributes. The
// content document uses the no-proxy variant, which carries no access hash.
TEST(InlineDecodeFakeDc, ThumbAndContentWebDocumentsDecodeWithAttributes) {
    FakeDc dc;
    ty::webDocument thumb;
    thumb.url = "https://cdn.example/thumb.jpg";
    thumb.access_hash = 555;
    thumb.size = 1234;
    thumb.mime_type = "image/jpeg";
    ty::documentAttributeImageSize size;
    size.w = 640;
    size.h = 480;
    thumb.attributes.push_back(attr(tl::DocumentAttribute{size}));

    ty::webDocumentNoProxy content;
    content.url = "https://example.org/clip";
    content.size = 99;
    content.mime_type = "video/mp4";
    ty::documentAttributeVideo video;
    video.duration = 3.5;
    video.w = 16;
    video.h = 9;
    content.attributes.push_back(attr(tl::DocumentAttribute{video}));
    ty::documentAttributeFilename name;
    name.file_name = "clip.mp4";
    content.attributes.push_back(attr(tl::DocumentAttribute{name}));

    ty::botInlineMessageText text;
    text.message = "look";
    ty::botInlineResult r = make_result("doc", "article", tl::BotInlineMessage{text});
    r.thumb = web_doc(thumb);
    r.content = web_doc_no_proxy(content);
    answer_with(dc, {wrap(r)});

    const std::string dir = temp_dir_name("docs");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();

    ASSERT_EQ(found.results.size(), 1u);
    const auto& out = found.results[0];
    ASSERT_TRUE(out.thumb.has_value());
    EXPECT_EQ(out.thumb->url, "https://cdn.example/thumb.jpg");
    EXPECT_EQ(out.thumb->access_hash, 555);
    EXPECT_EQ(out.thumb->size, 1234);
    EXPECT_EQ(out.thumb->mime_type, "image/jpeg");
    EXPECT_EQ(out.thumb->width, 640);
    EXPECT_EQ(out.thumb->height, 480);

    ASSERT_TRUE(out.content.has_value());
    EXPECT_EQ(out.content->url, "https://example.org/clip");
    EXPECT_EQ(out.content->size, 99);
    EXPECT_EQ(out.content->mime_type, "video/mp4");
    EXPECT_DOUBLE_EQ(out.content->duration, 3.5);
    EXPECT_EQ(out.content->width, 16);
    EXPECT_EQ(out.content->height, 9);
    EXPECT_EQ(out.content->file_name, "clip.mp4");
    std::filesystem::remove_all(dir);
}

// A web document attribute this client does not decode is recorded by its constructor id. The result is kept, and the
// attributes it does decode are still read.
TEST(InlineDecodeFakeDc, UnknownWebDocumentAttributeIsRecordedAndResultKept) {
    FakeDc dc;
    ty::webDocument thumb;
    thumb.url = "https://cdn.example/x.jpg";
    thumb.mime_type = "image/jpeg";
    ty::documentAttributeImageSize size;
    size.w = 100;
    size.h = 50;
    thumb.attributes.push_back(attr(tl::DocumentAttribute{size}));
    thumb.attributes.push_back(attr(tl::DocumentAttribute{ty::documentAttributeHasStickers{}}));

    ty::botInlineMessageText text;
    text.message = "x";
    ty::botInlineResult r = make_result("bad", "article", tl::BotInlineMessage{text});
    r.thumb = web_doc(thumb);
    answer_with(dc, {wrap(r)});

    const std::string dir = temp_dir_name("unknown-attr");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();
    std::filesystem::remove_all(dir);

    ASSERT_EQ(found.results.size(), 1u);
    ASSERT_TRUE(found.results[0].thumb.has_value());
    EXPECT_EQ(found.results[0].thumb->width, 100);
    EXPECT_EQ(found.results[0].thumb->height, 50);
    ASSERT_EQ(found.results[0].thumb->unknown_attribute_ids.size(), 1u);
    EXPECT_EQ(found.results[0].thumb->unknown_attribute_ids[0], ty::documentAttributeHasStickers::kId);
}

// A rich result reports its kind, direction, and the number of page blocks. The full TL object is kept in rich_raw.
TEST(InlineDecodeFakeDc, RichResultReportsBlockCountAndKeepsRawObject) {
    FakeDc dc;
    ty::richMessage body;
    body.rtl = true;
    body.part = false;
    body.blocks.push_back(std::make_shared<tl::PageBlock>(tl::PageBlock{ty::pageBlockUnsupported{}}));
    body.blocks.push_back(std::make_shared<tl::PageBlock>(tl::PageBlock{ty::pageBlockUnsupported{}}));
    ty::botInlineMessageRichMessage rich;
    rich.rich_message = std::make_shared<tl::RichMessage>(tl::RichMessage{body});
    answer_with(dc, {wrap(make_result("rich", "article", tl::BotInlineMessage{rich}))});

    const std::string dir = temp_dir_name("rich");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();

    ASSERT_EQ(found.results.size(), 1u);
    const auto& out = found.results[0];
    EXPECT_EQ(out.message_kind, InlineMessageKind::Rich);
    EXPECT_TRUE(out.rich_rtl);
    EXPECT_FALSE(out.rich_part);
    EXPECT_EQ(out.rich_block_count, 2u);
    ASSERT_NE(out.rich_raw, nullptr);
    const auto* raw = std::get_if<ty::richMessage>(&out.rich_raw->value);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->blocks.size(), 2u);
    std::filesystem::remove_all(dir);
}

// An invoice result carries the invoice photo as a web document with its dimensions.
TEST(InlineDecodeFakeDc, InvoiceResultDecodesPhotoAndAmounts) {
    FakeDc dc;
    ty::webDocument photo;
    photo.url = "https://cdn.example/invoice.jpg";
    photo.mime_type = "image/jpeg";
    ty::documentAttributeImageSize size;
    size.w = 300;
    size.h = 200;
    photo.attributes.push_back(attr(tl::DocumentAttribute{size}));

    ty::botInlineMessageMediaInvoice invoice;
    invoice.title = "bill";
    invoice.description = "one item";
    invoice.currency = "EUR";
    invoice.total_amount = 1234;
    invoice.test = true;
    invoice.photo = web_doc(photo);
    answer_with(dc, {wrap(make_result("inv", "invoice", tl::BotInlineMessage{invoice}))});

    const std::string dir = temp_dir_name("invoice");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();

    ASSERT_EQ(found.results.size(), 1u);
    const auto& out = found.results[0];
    EXPECT_EQ(out.message_kind, InlineMessageKind::Invoice);
    EXPECT_EQ(out.invoice_title, "bill");
    EXPECT_EQ(out.invoice_description, "one item");
    EXPECT_EQ(out.currency, "EUR");
    EXPECT_EQ(out.total_amount, 1234);
    EXPECT_TRUE(out.invoice_test);
    ASSERT_TRUE(out.invoice_photo.has_value());
    EXPECT_EQ(out.invoice_photo->url, "https://cdn.example/invoice.jpg");
    EXPECT_EQ(out.invoice_photo->width, 300);
    EXPECT_EQ(out.invoice_photo->height, 200);
    std::filesystem::remove_all(dir);
}

// A widget registered by the bot decodes its id and public data hash. The payload bytes and chat data are kept too.
TEST(InlineDecodeFakeDc, RegisteredWidgetDecodesIdHashAndPayload) {
    FakeDc dc;
    ty::botInlineMessageWidget widget;
    widget.width = 10;
    widget.height = 20;
    widget.payload = Bytes{'x', 'y', 'z'};
    widget.public_data = std::string("pub");
    widget.public_data_hash = 77;
    widget.chat_data = std::string("chat");
    widget.widget_id = 4242;
    widget.message = "widget";
    answer_with(dc, {wrap(make_result("widget", "widget", tl::BotInlineMessage{widget}))});

    const std::string dir = temp_dir_name("widget");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();

    ASSERT_EQ(found.results.size(), 1u);
    const auto& out = found.results[0];
    EXPECT_EQ(out.message_kind, InlineMessageKind::Widget);
    EXPECT_EQ(out.message_text, "widget");
    EXPECT_EQ(out.widget_width, 10);
    EXPECT_EQ(out.widget_height, 20);
    EXPECT_EQ(out.widget_payload, (std::vector<std::uint8_t>{'x', 'y', 'z'}));
    EXPECT_EQ(out.public_data, std::optional<std::string>("pub"));
    EXPECT_EQ(out.public_data_hash, std::optional<std::int64_t>(77));
    EXPECT_EQ(out.chat_data, std::optional<std::string>("chat"));
    EXPECT_EQ(out.widget_id, std::optional<std::int64_t>(4242));
    std::filesystem::remove_all(dir);
}

// A reply keyboard on a result decodes into typed rows, and no inline keyboard is reported for it.
TEST(InlineDecodeFakeDc, ReplyKeyboardOnResultDecodesTypedButtons) {
    FakeDc dc;
    ty::keyboardButton phone;
    phone.text = "send phone";
    phone.type = std::make_shared<tl::ButtonType>(tl::ButtonType{ty::buttonTypeRequestPhone{}});
    ty::keyboardButtonRow row;
    row.buttons.push_back(std::make_shared<tl::KeyboardButton>(tl::KeyboardButton{phone}));
    ty::replyKeyboardMarkup keyboard;
    keyboard.rows.push_back(std::make_shared<tl::KeyboardButtonRow>(tl::KeyboardButtonRow{row}));

    ty::botInlineMessageText text;
    text.message = "share";
    text.reply_markup = std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{keyboard});
    answer_with(dc, {wrap(make_result("reply", "article", tl::BotInlineMessage{text}))});

    const std::string dir = temp_dir_name("reply");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(555), user_peer(556), "q");
    client.disconnect();

    ASSERT_EQ(found.results.size(), 1u);
    const auto& out = found.results[0];
    EXPECT_EQ(out.message_kind, InlineMessageKind::Text);
    EXPECT_FALSE(out.inline_markup.has_value());
    ASSERT_TRUE(out.reply_markup.has_value());
    ASSERT_EQ(out.reply_markup->typed_rows.size(), 1u);
    ASSERT_EQ(out.reply_markup->typed_rows[0].size(), 1u);
    EXPECT_EQ(out.reply_markup->typed_rows[0][0].text, "send phone");
    EXPECT_EQ(out.reply_markup->typed_rows[0][0].kind, ReplyButtonKind::RequestPhone);
    std::filesystem::remove_all(dir);
}
