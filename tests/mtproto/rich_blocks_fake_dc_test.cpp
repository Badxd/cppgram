// Typed rich result page blocks, depth limit, and local-file upload for rich message bodies. Scripted fake DC, offline
// only. NOT RUN in this session. Each test checks the decoded fields or the request the client sends.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::EphemeralMessageOptions;
using cppgram::InlineMessageKind;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::RichBlockInfo;
using cppgram::RichBlockKind;
using cppgram::RichMessageFileInfo;
using cppgram::RichMessageInput;
using cppgram::testing::FakeDc;
using cppgram::mtproto::Bytes;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

constexpr std::uint32_t kSendRich = 1u << 4;
constexpr std::uint32_t kPeerFlag = 1u << 8;

std::string temp_dir_name(const char* tag) {
    static std::atomic<int> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cppgram-rich-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

std::shared_ptr<tl::RichText> plain(const std::string& s) {
    ty::textPlain p;
    p.text = s;
    return std::make_shared<tl::RichText>(tl::RichText{p});
}

std::shared_ptr<tl::RichText> bold(const std::string& s) {
    ty::textBold b;
    b.text = plain(s);
    return std::make_shared<tl::RichText>(tl::RichText{b});
}

std::shared_ptr<tl::PageBlock> block(ty::pageBlockUnsupported) {
    return std::make_shared<tl::PageBlock>(tl::PageBlock{ty::pageBlockUnsupported{}});
}

std::shared_ptr<tl::PageBlock> paragraph(std::shared_ptr<tl::RichText> text) {
    ty::pageBlockParagraph p;
    p.text = std::move(text);
    return std::make_shared<tl::PageBlock>(tl::PageBlock{p});
}

// Returns the rich body's blocks as the bot would receive them in a rich result.
std::vector<RichBlockInfo> decode_rich_blocks(FakeDc& dc, std::vector<std::shared_ptr<tl::PageBlock>> blocks,
                                              const std::string& dir) {
    ty::richMessage body;
    body.blocks = std::move(blocks);
    ty::botInlineMessageRichMessage rich;
    rich.rich_message = std::make_shared<tl::RichMessage>(tl::RichMessage{body});

    ty::botInlineResult r;
    r.id = "rich";
    r.type = "article";
    r.send_message = std::make_shared<tl::BotInlineMessage>(tl::BotInlineMessage{rich});
    auto result = std::make_shared<tl::BotInlineResult>(tl::BotInlineResult{r});

    dc.set_handler([result](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getInlineBotResults::kId) return false;
        ty::messages_botResults response;
        response.query_id = 1;
        response.results = {result};
        response.serialize(out);
        return true;
    });
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    const auto found = client.get_inline_bot_results(user_peer(1), user_peer(2), "q");
    client.disconnect();
    if (found.results.empty()) return {};
    EXPECT_EQ(found.results[0].message_kind, InlineMessageKind::Rich);
    return found.results[0].rich_blocks;
}

}  // namespace

// Every block kind decodes to its public kind, with its plain text, label, media id and children. Each block keeps
// the TL constructor id and the raw TL object.
TEST(RichBlocksFakeDc, TypedBlocksDecodeWithTextChildrenAndIds) {
    FakeDc dc;
    const std::string dir = temp_dir_name("typed");

    ty::pageBlockTitle title;
    title.text = plain("Title");

    ty::textConcat concat;
    concat.texts = {plain("a "), bold("b")};
    ty::pageBlockParagraph para;
    para.text = std::make_shared<tl::RichText>(tl::RichText{concat});

    ty::pageListItemText item_text;
    item_text.checked = true;
    item_text.text = plain("one");
    ty::pageListItemBlocks item_blocks;
    item_blocks.blocks = {paragraph(plain("nested"))};
    ty::pageBlockList list;
    list.items = {std::make_shared<tl::PageListItem>(tl::PageListItem{item_text}),
                  std::make_shared<tl::PageListItem>(tl::PageListItem{item_blocks})};

    ty::pageListOrderedItemText ordered_text;
    ordered_text.num = std::string("2.");
    ordered_text.text = plain("two");
    ty::pageBlockOrderedList ordered;
    ordered.items = {std::make_shared<tl::PageListOrderedItem>(tl::PageListOrderedItem{ordered_text})};

    ty::pageCaption caption;
    caption.text = plain("cap");
    caption.credit = plain("");  // pageCaption carries text and credit unconditionally on the wire
    ty::pageBlockPhoto photo;
    photo.photo_id = 77;
    photo.caption = std::make_shared<tl::PageCaption>(tl::PageCaption{caption});

    ty::pageBlockDetails details;
    details.title = plain("More");
    details.blocks = {paragraph(plain("inside"))};

    ty::pageBlockCover cover;
    cover.cover = block(ty::pageBlockUnsupported{});

    const auto decoded = decode_rich_blocks(
        dc,
        {std::make_shared<tl::PageBlock>(tl::PageBlock{title}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{para}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{list}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{ordered}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{photo}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{details}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{cover}),
         block(ty::pageBlockUnsupported{})},
        dir);
    std::filesystem::remove_all(dir);

    ASSERT_EQ(decoded.size(), 8u);
    EXPECT_EQ(decoded[0].kind, RichBlockKind::Title);
    EXPECT_EQ(decoded[0].text, "Title");
    EXPECT_EQ(decoded[0].constructor_id, ty::pageBlockTitle::kId);
    ASSERT_NE(decoded[0].raw, nullptr);

    EXPECT_EQ(decoded[1].kind, RichBlockKind::Paragraph);
    EXPECT_EQ(decoded[1].text, "a b") << "formatting is removed, text is joined";

    EXPECT_EQ(decoded[2].kind, RichBlockKind::List);
    ASSERT_EQ(decoded[2].items.size(), 2u);
    EXPECT_EQ(decoded[2].items[0].kind, RichBlockKind::ListItem);
    EXPECT_EQ(decoded[2].items[0].text, "one");
    EXPECT_TRUE(decoded[2].items[0].checked);
    EXPECT_FALSE(decoded[2].items[0].checkbox);
    ASSERT_EQ(decoded[2].items[1].items.size(), 1u) << "a list item of blocks keeps its child blocks";
    EXPECT_EQ(decoded[2].items[1].items[0].kind, RichBlockKind::Paragraph);
    EXPECT_EQ(decoded[2].items[1].items[0].text, "nested");

    EXPECT_EQ(decoded[3].kind, RichBlockKind::OrderedList);
    ASSERT_EQ(decoded[3].items.size(), 1u);
    EXPECT_EQ(decoded[3].items[0].label, "2.");
    EXPECT_EQ(decoded[3].items[0].text, "two");

    EXPECT_EQ(decoded[4].kind, RichBlockKind::Photo);
    EXPECT_EQ(decoded[4].media_id, 77);
    EXPECT_EQ(decoded[4].caption, "cap");

    EXPECT_EQ(decoded[5].kind, RichBlockKind::Details);
    EXPECT_EQ(decoded[5].text, "More");
    ASSERT_EQ(decoded[5].items.size(), 1u);
    EXPECT_EQ(decoded[5].items[0].text, "inside");

    EXPECT_EQ(decoded[6].kind, RichBlockKind::Cover);
    ASSERT_EQ(decoded[6].items.size(), 1u);
    EXPECT_EQ(decoded[6].items[0].kind, RichBlockKind::Unsupported);

    EXPECT_EQ(decoded[7].kind, RichBlockKind::Unsupported);
    EXPECT_EQ(decoded[7].constructor_id, ty::pageBlockUnsupported::kId);
}

// Block nesting deeper than the decoder allows is a typed error, and the result is not returned half-decoded.
TEST(RichBlocksFakeDc, DeeplyNestedBlocksThrowSerializationError) {
    FakeDc dc;
    std::shared_ptr<tl::PageBlock> inner = block(ty::pageBlockUnsupported{});
    for (int i = 0; i < 40; ++i) {
        ty::pageBlockDetails d;
        d.title = plain("t");  // title is required on the wire (no flag)
        d.blocks = {inner};
        inner = std::make_shared<tl::PageBlock>(tl::PageBlock{d});
    }
    ty::richMessage body;
    body.blocks = {inner};
    ty::botInlineMessageRichMessage rich;
    rich.rich_message = std::make_shared<tl::RichMessage>(tl::RichMessage{body});
    ty::botInlineResult r;
    r.id = "deep";
    r.type = "article";
    r.send_message = std::make_shared<tl::BotInlineMessage>(tl::BotInlineMessage{rich});
    auto result = std::make_shared<tl::BotInlineResult>(tl::BotInlineResult{r});
    dc.set_handler([result](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getInlineBotResults::kId) return false;
        ty::messages_botResults response;
        response.query_id = 1;
        response.results = {result};
        response.serialize(out);
        return true;
    });
    const std::string dir = temp_dir_name("deep");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    EXPECT_THROW(client.get_inline_bot_results(user_peer(1), user_peer(2), "q"), cppgram::errors::SerializationError);
    client.disconnect();
    std::filesystem::remove_all(dir);
}

// A local file in a rich message body is uploaded (save_file, then messages.uploadMedia with the bot's own peer), and
// the returned photo id is the one the rich body sends.
TEST(RichBlocksFakeDc, RichBodyLocalPhotoIsUploadedBeforeSend) {
    FakeDc dc;
    const std::string dir = temp_dir_name("rich-upload");
    const auto path = (std::filesystem::path(dir) / "pic.jpg").string();
    { std::ofstream out(path, std::ios::binary); out << std::string(100, 'p'); }

    int upload_parts = 0;
    bool upload_media_self = false;
    std::string sent_rich_html;
    std::string sent_file_id;
    std::int64_t sent_photo_id = 0;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            ++upload_parts;
            out.write_int32(0x997275b5u);  // boolTrue
            return true;
        }
        if (id == fn::messages_uploadMedia::kId) {
            // messages.uploadMedia starts with a flags word; bit 0 is business_connection_id, which the client does not set.
            const std::uint32_t req_flags = args.read_uint32();
            if (req_flags & 0x1u) (void)args.read_string();
            const auto peer = tl::InputPeer::read(args);
            upload_media_self = std::holds_alternative<ty::inputPeerSelf>(peer.value);
            ty::photo ph;
            ph.id = 5555;
            ph.access_hash = 6666;
            ph.file_reference = Bytes{9, 8};
            ph.date = 1;
            ph.dc_id = 2;
            ty::messageMediaPhoto mm;
            mm.photo = std::make_shared<tl::Photo>(tl::Photo{ph});
            mm.serialize(out);
            return true;
        }
        if (id != fn::ephemeral_sendMessage::kId) return false;
        const std::uint32_t flags = args.read_uint32();
        if (flags & kPeerFlag) (void)tl::InputPeer::read(args);
        (void)tl::InputUser::read(args);
        const std::string message = args.read_string();
        (void)message;
        if (flags & kSendRich) {
            const auto rich = tl::InputRichMessage::read(args);
            if (const auto* html = std::get_if<ty::inputRichMessageHTML>(&rich.value)) {
                sent_rich_html = html->html;
                if (html->files && !html->files->empty()) {
                    if (const auto* f = std::get_if<ty::inputRichFilePhoto>(&(*html->files)[0]->value)) {
                        sent_file_id = f->id;
                        if (f->photo) {
                            if (const auto* ip = std::get_if<ty::inputPhoto>(&f->photo->value)) sent_photo_id = ip->id;
                        }
                    }
                }
            }
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    EphemeralMessageOptions options;
    RichMessageInput rich;
    rich.format = RichMessageInput::Format::Html;
    rich.body = "<img src=\"pic\">";
    RichMessageFileInfo file;
    file.id = "pic";
    file.path = path;
    file.as_photo = true;
    rich.files.push_back(file);
    options.rich_message = rich;
    const bool ok = client.ephemeral_send_message(user_peer(777), "", options);
    client.disconnect();
    std::filesystem::remove_all(dir);

    EXPECT_TRUE(ok);
    EXPECT_GE(upload_parts, 1) << "the local file must be uploaded first";
    EXPECT_TRUE(upload_media_self) << "messages.uploadMedia must use the bot's own peer";
    EXPECT_EQ(sent_rich_html, "<img src=\"pic\">");
    EXPECT_EQ(sent_file_id, "pic");
    EXPECT_EQ(sent_photo_id, 5555) << "the body must send the photo id Telegram returned";
}

// A rich file that gives both a path and cached media is refused before any upload or request.
TEST(RichBlocksFakeDc, RichFileWithPathAndMediaIsRefusedBeforeRpc) {
    FakeDc dc;
    int requests = 0;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        ++requests;
        if (id == fn::ephemeral_sendMessage::kId) {
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });
    const std::string dir = temp_dir_name("both");
    Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
    client.connect();
    requests = 0;  // count only what the refused call sends, not the connect handshake
    EphemeralMessageOptions options;
    RichMessageInput rich;
    rich.body = "<img src=\"x\">";
    RichMessageFileInfo file;
    file.id = "x";
    file.path = (std::filesystem::path(dir) / "x.jpg").string();
    file.media.kind = cppgram::MediaKind::Photo;
    file.media.id = 1;
    rich.files.push_back(file);
    options.rich_message = rich;
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "", options), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove_all(dir);
    EXPECT_EQ(requests, 0) << "a refused rich file must not reach the server";
}

// Table, related-article, map and channel blocks decode into their public fields, not only their raw TL. The values
// set here are the ones the decoder must return.
TEST(RichBlocksFakeDc, TableRelatedMapAndChannelDecodeIntoPublicFields) {
    FakeDc dc;
    const std::string dir = temp_dir_name("typed-fields");

    ty::pageTableCell head;
    head.header = true;
    head.align_center = true;
    head.text = plain("h");
    head.colspan = 2;
    ty::pageTableCell body;
    body.valign_bottom = true;
    body.rowspan = 3;
    body.text = plain("b");
    ty::pageTableRow row;
    row.cells = {std::make_shared<tl::PageTableCell>(tl::PageTableCell{head}),
                 std::make_shared<tl::PageTableCell>(tl::PageTableCell{body})};
    ty::pageBlockTable table;
    table.bordered = true;
    table.striped = true;
    table.title = plain("tt");
    table.rows = {std::make_shared<tl::PageTableRow>(tl::PageTableRow{row})};

    ty::pageRelatedArticle article;
    article.url = "https://example.org/a";
    article.webpage_id = 9;
    article.title = std::string("A");
    article.published_date = 1700000000;
    ty::pageBlockRelatedArticles related;
    related.title = plain("more");
    related.articles = {std::make_shared<tl::PageRelatedArticle>(tl::PageRelatedArticle{article})};

    ty::geoPoint geo;
    geo.lat = 12.5;
    geo.long_ = -3.25;
    ty::pageBlockMap map_block;
    ty::pageCaption map_caption;
    map_caption.text = plain("map");
    map_caption.credit = plain("");
    map_block.caption = std::make_shared<tl::PageCaption>(tl::PageCaption{map_caption});
    map_block.geo = std::make_shared<tl::GeoPoint>(tl::GeoPoint{geo});
    map_block.zoom = 13;
    map_block.w = 300;
    map_block.h = 200;

    ty::chatForbidden forbidden;
    forbidden.id = 4444;
    forbidden.title = "gone";
    ty::pageBlockChannel channel;
    channel.channel = std::make_shared<tl::Chat>(tl::Chat{forbidden});

    const auto decoded = decode_rich_blocks(
        dc,
        {std::make_shared<tl::PageBlock>(tl::PageBlock{table}), std::make_shared<tl::PageBlock>(tl::PageBlock{related}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{map_block}),
         std::make_shared<tl::PageBlock>(tl::PageBlock{channel})},
        dir);
    ASSERT_EQ(decoded.size(), 4u);

    EXPECT_EQ(decoded[0].kind, RichBlockKind::Table);
    EXPECT_EQ(decoded[0].text, "tt");
    EXPECT_TRUE(decoded[0].bordered);
    EXPECT_TRUE(decoded[0].striped);
    EXPECT_FALSE(decoded[0].compact);
    ASSERT_EQ(decoded[0].rows.size(), 1u);
    ASSERT_EQ(decoded[0].rows[0].size(), 2u);
    EXPECT_EQ(decoded[0].rows[0][0].text, "h");
    EXPECT_TRUE(decoded[0].rows[0][0].header);
    EXPECT_TRUE(decoded[0].rows[0][0].align_center);
    EXPECT_EQ(decoded[0].rows[0][0].colspan, 2);
    EXPECT_EQ(decoded[0].rows[0][0].rowspan, 1);
    EXPECT_EQ(decoded[0].rows[0][1].text, "b");
    EXPECT_FALSE(decoded[0].rows[0][1].header);
    EXPECT_TRUE(decoded[0].rows[0][1].valign_bottom);
    EXPECT_EQ(decoded[0].rows[0][1].colspan, 1);
    EXPECT_EQ(decoded[0].rows[0][1].rowspan, 3);

    EXPECT_EQ(decoded[1].kind, RichBlockKind::RelatedArticles);
    EXPECT_EQ(decoded[1].text, "more");
    ASSERT_EQ(decoded[1].articles.size(), 1u);
    EXPECT_EQ(decoded[1].articles[0].url, "https://example.org/a");
    EXPECT_EQ(decoded[1].articles[0].webpage_id, 9);
    EXPECT_EQ(decoded[1].articles[0].title, "A");
    EXPECT_EQ(decoded[1].articles[0].description, "");
    EXPECT_EQ(decoded[1].articles[0].published_date, std::optional<std::int32_t>(1700000000));
    EXPECT_FALSE(decoded[1].articles[0].photo_id.has_value());

    EXPECT_EQ(decoded[2].kind, RichBlockKind::Map);
    EXPECT_TRUE(decoded[2].has_geo);
    EXPECT_DOUBLE_EQ(decoded[2].latitude, 12.5);
    EXPECT_DOUBLE_EQ(decoded[2].longitude, -3.25);
    EXPECT_EQ(decoded[2].zoom, 13);
    EXPECT_EQ(decoded[2].width, 300);
    EXPECT_EQ(decoded[2].height, 200);

    EXPECT_EQ(decoded[3].kind, RichBlockKind::Channel);
    EXPECT_EQ(decoded[3].channel_id, 4444);
    EXPECT_EQ(decoded[3].channel_title, "gone");
    EXPECT_EQ(decoded[3].channel_username, "");
}

// A table cell whose colspan or rowspan is below 1 is rejected with SerializationError. Each case is sent as a
// rich inline result and must fail at decode time, not produce a block with an invalid span. Offline only.
TEST(RichBlocksFakeDc, TableCellSpansBelowOneAreRejected) {
    auto expect_rejected = [](const ty::pageTableCell& cell) {
        FakeDc dc;
        ty::pageTableRow row;
        row.cells = {std::make_shared<tl::PageTableCell>(tl::PageTableCell{cell})};
        ty::pageBlockTable table;
        table.title = plain("");  // title is required on the wire (no flag)
        table.rows = {std::make_shared<tl::PageTableRow>(tl::PageTableRow{row})};
        ty::richMessage body;
        body.blocks = {std::make_shared<tl::PageBlock>(tl::PageBlock{table})};
        ty::botInlineMessageRichMessage rich;
        rich.rich_message = std::make_shared<tl::RichMessage>(tl::RichMessage{body});
        ty::botInlineResult r;
        r.id = "span";
        r.type = "article";
        r.send_message = std::make_shared<tl::BotInlineMessage>(tl::BotInlineMessage{rich});
        auto result = std::make_shared<tl::BotInlineResult>(tl::BotInlineResult{r});
        dc.set_handler([result](std::uint32_t id, TlReader&, TlWriter& out) {
            if (id != fn::messages_getInlineBotResults::kId) return false;
            ty::messages_botResults response;
            response.query_id = 1;
            response.results = {result};
            response.serialize(out);
            return true;
        });
        const std::string dir = temp_dir_name("span");
        Client client(options_for(dc, (std::filesystem::path(dir) / "s.session").string()));
        client.connect();
        EXPECT_THROW(client.get_inline_bot_results(user_peer(1), user_peer(2), "q"),
                     cppgram::errors::SerializationError);
        client.disconnect();
        std::filesystem::remove_all(dir);
    };

    ty::pageTableCell zero_colspan;
    zero_colspan.text = plain("z");
    zero_colspan.colspan = 0;
    expect_rejected(zero_colspan);

    ty::pageTableCell negative_rowspan;
    negative_rowspan.text = plain("n");
    negative_rowspan.rowspan = -2;
    expect_rejected(negative_rowspan);
}
