// Invoice, widget and reply-keyboard inline results sent with messages.sendWebViewResultMessage, against a scripted
// fake DC. Offline only. NOT RUN in this session. The handler reads the InputBotInlineResult the client sends and the
// tests check its fields. Nothing here contacts a payment provider, and no payment is completed.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
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
using cppgram::InlineButton;
using cppgram::InlineTextResultInfo;
using cppgram::InvoiceInfo;
using cppgram::LabeledPriceInfo;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::ReplyKeyboard;
using cppgram::ReplyButtonKind;
using cppgram::ReplyKeyboardButton;
using cppgram::WidgetSendInfo;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

namespace {

std::string temp_dir_name(const char* tag) {
    static std::atomic<int> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cppgram-inlsend-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

InlineTextResultInfo base_result(InlineTextResultInfo::Kind kind) {
    InlineTextResultInfo r;
    r.kind = kind;
    r.id = "result-1";
    r.type = "article";
    return r;
}

InvoiceInfo valid_invoice() {
    InvoiceInfo inv;
    inv.title = "Coffee";
    inv.description = "One cup";
    inv.currency = "USD";
    inv.prices = {LabeledPriceInfo{"cup", 250}, LabeledPriceInfo{"tip", 50}};
    inv.provider = "test-provider";
    inv.payload = {'o', 'r', 'd', '1'};
    inv.test = true;
    inv.shipping_address_requested = true;
    inv.max_tip_amount = 1000;
    inv.suggested_tip_amounts = std::vector<std::int64_t>{100, 500};
    return inv;
}

}  // namespace

// The sent invoice carries every field of the public invoice. An empty provider_data is sent as an empty JSON object.
TEST(InlineSendFakeDc, InvoiceResultCarriesInvoiceFieldsAndPayload) {
    FakeDc dc;
    std::string query_id;
    bool is_invoice = false;
    ty::inputBotInlineMessageMediaInvoice msg{};
    ty::invoice inv{};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::messages_sendWebViewResultMessage::kId) return false;
        query_id = args.read_string();
        const auto result = tl::InputBotInlineResult::read(args);
        if (const auto* plain = std::get_if<ty::inputBotInlineResult>(&result.value)) {
            if (plain->send_message) {
                if (const auto* m = std::get_if<ty::inputBotInlineMessageMediaInvoice>(&plain->send_message->value)) {
                    is_invoice = true;
                    msg = *m;
                    if (m->invoice) {
                        if (const auto* v = std::get_if<ty::invoice>(&m->invoice->value)) inv = *v;
                    }
                }
            }
        }
        ty::webViewMessageSent sent;
        sent.serialize(out);
        return true;
    });

    const std::string dir = temp_dir_name("invoice");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    InlineTextResultInfo r = base_result(InlineTextResultInfo::Kind::Invoice);
    r.invoice = valid_invoice();
    client.send_web_view_result_message("query-9", r);
    client.disconnect();

    EXPECT_EQ(query_id, "query-9");
    ASSERT_TRUE(is_invoice);
    EXPECT_EQ(msg.title, "Coffee");
    EXPECT_EQ(msg.description, "One cup");
    EXPECT_EQ(msg.provider, "test-provider");
    EXPECT_EQ(msg.payload, (cppgram::mtproto::Bytes{'o', 'r', 'd', '1'}));
    ASSERT_TRUE(msg.provider_data);
    const auto* data = std::get_if<ty::dataJSON>(&msg.provider_data->value);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->data, "{}") << "empty provider data is sent as an empty JSON object";
    EXPECT_EQ(inv.currency, "USD");
    EXPECT_TRUE(inv.test);
    EXPECT_TRUE(inv.shipping_address_requested);
    EXPECT_FALSE(inv.phone_requested);
    ASSERT_EQ(inv.prices.size(), 2u);
    const auto* cup = std::get_if<ty::labeledPrice>(&inv.prices[0]->value);
    const auto* tip = std::get_if<ty::labeledPrice>(&inv.prices[1]->value);
    ASSERT_NE(cup, nullptr);
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(cup->label, "cup");
    EXPECT_EQ(cup->amount, 250);
    EXPECT_EQ(tip->label, "tip");
    EXPECT_EQ(tip->amount, 50);
    EXPECT_EQ(inv.max_tip_amount, std::optional<std::int64_t>(1000));
    ASSERT_TRUE(inv.suggested_tip_amounts.has_value());
    EXPECT_EQ(*inv.suggested_tip_amounts, (std::vector<std::int64_t>{100, 500}));
    std::filesystem::remove_all(dir);
}

// Invoice input that cannot be valid is refused before the RPC is sent.
TEST(InlineSendFakeDc, InvalidInvoiceFailsBeforeRpc) {
    FakeDc dc;
    int sends = 0;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_sendWebViewResultMessage::kId) return false;
        ++sends;
        ty::webViewMessageSent sent;
        sent.serialize(out);
        return true;
    });
    const std::string dir = temp_dir_name("bad-invoice");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();

    auto attempt = [&](auto mutate) {
        InlineTextResultInfo r = base_result(InlineTextResultInfo::Kind::Invoice);
        r.invoice = valid_invoice();
        mutate(*r.invoice);
        return r;
    };
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.provider.clear(); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.currency = "US"; })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.prices.clear(); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.title.clear(); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.title.assign(33, 't'); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.description.assign(256, 'd'); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.payload.clear(); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.payload.assign(129, 1); })),
                 std::invalid_argument);
    EXPECT_THROW(client.send_web_view_result_message("q", attempt([](InvoiceInfo& i) { i.prices[0].label.clear(); })),
                 std::invalid_argument);
    InlineTextResultInfo missing = base_result(InlineTextResultInfo::Kind::Invoice);
    EXPECT_THROW(client.send_web_view_result_message("q", missing), std::invalid_argument);
    client.disconnect();
    EXPECT_EQ(sends, 0);
    std::filesystem::remove_all(dir);
}

// An inline widget sends its html, size, flags and public data. A widget registered by id sends the id and chat data.
TEST(InlineSendFakeDc, WidgetHtmlAndIdVariantsAreSentCorrectly) {
    FakeDc dc;
    std::vector<int> kinds;  // 1 = html widget, 2 = widget id
    ty::inputBotInlineMessageWidget html_widget{};
    ty::inputBotInlineMessageWidgetId id_widget{};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::messages_sendWebViewResultMessage::kId) return false;
        (void)args.read_string();
        const auto result = tl::InputBotInlineResult::read(args);
        if (const auto* plain = std::get_if<ty::inputBotInlineResult>(&result.value)) {
            if (plain->send_message) {
                if (const auto* w = std::get_if<ty::inputBotInlineMessageWidget>(&plain->send_message->value)) {
                    kinds.push_back(1);
                    html_widget = *w;
                } else if (const auto* wi = std::get_if<ty::inputBotInlineMessageWidgetId>(&plain->send_message->value)) {
                    kinds.push_back(2);
                    id_widget = *wi;
                }
            }
        }
        ty::webViewMessageSent sent;
        sent.serialize(out);
        return true;
    });

    const std::string dir = temp_dir_name("widget");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();

    InlineTextResultInfo html = base_result(InlineTextResultInfo::Kind::Widget);
    html.text = "widget caption";
    WidgetSendInfo w;
    w.html = "<div>hi</div>";
    w.width = 320;
    w.height = 240;
    w.allow_queries = true;
    w.shared_widget = true;
    w.public_data = std::string("pub");
    w.chat_data = std::string("chat");
    html.widget = w;
    client.send_web_view_result_message("q", html);

    InlineTextResultInfo by_id = base_result(InlineTextResultInfo::Kind::Widget);
    WidgetSendInfo wid;
    wid.widget_id = 987654;
    wid.chat_data = std::string("c2");
    by_id.widget = wid;
    client.send_web_view_result_message("q", by_id);

    client.disconnect();
    ASSERT_EQ(kinds, (std::vector<int>{1, 2}));
    EXPECT_EQ(html_widget.html, "<div>hi</div>");
    EXPECT_EQ(html_widget.width, 320);
    EXPECT_EQ(html_widget.height, 240);
    EXPECT_TRUE(html_widget.allow_queries);
    EXPECT_TRUE(html_widget.shared_widget);
    EXPECT_EQ(html_widget.public_data, std::optional<std::string>("pub"));
    EXPECT_EQ(html_widget.chat_data, std::optional<std::string>("chat"));
    EXPECT_EQ(html_widget.message, "widget caption");
    EXPECT_EQ(id_widget.widget_id, 987654);
    EXPECT_EQ(id_widget.chat_data, std::optional<std::string>("c2"));
    std::filesystem::remove_all(dir);
}

// Widget input that mixes the two forms, or gives html without a size, is refused before the RPC.
TEST(InlineSendFakeDc, InvalidWidgetFailsBeforeRpc) {
    FakeDc dc;
    int sends = 0;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_sendWebViewResultMessage::kId) return false;
        ++sends;
        ty::webViewMessageSent sent;
        sent.serialize(out);
        return true;
    });
    const std::string dir = temp_dir_name("bad-widget");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();

    InlineTextResultInfo both = base_result(InlineTextResultInfo::Kind::Widget);
    WidgetSendInfo w1;
    w1.widget_id = 5;
    w1.html = "<p>x</p>";
    both.widget = w1;
    EXPECT_THROW(client.send_web_view_result_message("q", both), std::invalid_argument);

    InlineTextResultInfo id_with_size = base_result(InlineTextResultInfo::Kind::Widget);
    WidgetSendInfo w2;
    w2.widget_id = 5;
    w2.width = 100;
    id_with_size.widget = w2;
    EXPECT_THROW(client.send_web_view_result_message("q", id_with_size), std::invalid_argument);

    InlineTextResultInfo no_size = base_result(InlineTextResultInfo::Kind::Widget);
    WidgetSendInfo w3;
    w3.html = "<p>x</p>";
    no_size.widget = w3;
    EXPECT_THROW(client.send_web_view_result_message("q", no_size), std::invalid_argument);

    InlineTextResultInfo none = base_result(InlineTextResultInfo::Kind::Widget);
    EXPECT_THROW(client.send_web_view_result_message("q", none), std::invalid_argument);

    client.disconnect();
    EXPECT_EQ(sends, 0);
    std::filesystem::remove_all(dir);
}

// A reply keyboard on an inline result is sent as replyKeyboardMarkup with its typed button. Giving an inline keyboard
// and a reply keyboard together is refused.
TEST(InlineSendFakeDc, ReplyKeyboardOnResultIsSentAndAmbiguityRefused) {
    FakeDc dc;
    bool has_reply = false;
    bool phone_button = false;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::messages_sendWebViewResultMessage::kId) return false;
        (void)args.read_string();
        const auto result = tl::InputBotInlineResult::read(args);
        if (const auto* plain = std::get_if<ty::inputBotInlineResult>(&result.value)) {
            if (plain->send_message) {
                if (const auto* t = std::get_if<ty::inputBotInlineMessageText>(&plain->send_message->value)) {
                    if (t->reply_markup && *t->reply_markup) {
                        if (const auto* rk = std::get_if<ty::replyKeyboardMarkup>(&(*t->reply_markup)->value)) {
                            has_reply = true;
                            if (!rk->rows.empty() && rk->rows[0]) {
                                const auto* row = std::get_if<ty::keyboardButtonRow>(&rk->rows[0]->value);
                                if (row && !row->buttons.empty() && row->buttons[0]) {
                                    if (const auto* kb = std::get_if<ty::keyboardButton>(&row->buttons[0]->value)) {
                                        phone_button =
                                            kb->type && std::holds_alternative<ty::buttonTypeRequestPhone>(kb->type->value);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        ty::webViewMessageSent sent;
        sent.serialize(out);
        return true;
    });
    const std::string dir = temp_dir_name("reply");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();

    InlineTextResultInfo r = base_result(InlineTextResultInfo::Kind::Text);
    r.text = "share please";
    ReplyKeyboard reply;
    ReplyKeyboardButton phone;
    phone.kind = ReplyButtonKind::RequestPhone;
    phone.text = "send phone";
    reply.typed_rows = {{phone}};
    r.reply_markup = reply;
    client.send_web_view_result_message("q", r);

    InlineTextResultInfo both = base_result(InlineTextResultInfo::Kind::Text);
    both.text = "x";
    both.inline_markup = cppgram::InlineKeyboard{{InlineButton::url("a", "https://a")}};
    both.reply_markup = reply;
    EXPECT_THROW(client.send_web_view_result_message("q", both), std::invalid_argument);

    client.disconnect();
    EXPECT_TRUE(has_reply);
    EXPECT_TRUE(phone_button);
    std::filesystem::remove_all(dir);
}
