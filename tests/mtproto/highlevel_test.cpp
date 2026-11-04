#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "cppgram/client.h"
#include "cppgram/convert.h"
#include "cppgram/filters.h"
#include "cppgram/mtproto/schema/api.h"

using namespace cppgram;
using mtproto::Bytes;
using mtproto::TlWriter;
namespace tl = mtproto::tl;
namespace ty = mtproto::tl::types;

namespace {

Message make_text(const std::string& text) {
    Message m;
    m.text = text;
    return m;
}

Bytes bytes_of(const TlWriter& w) {
    return w.bytes();
}

std::shared_ptr<tl::User> user_obj(std::int64_t id, std::int64_t hash, const std::string& first) {
    ty::user u;
    u.id = id;
    u.access_hash = hash;
    u.first_name = first;
    return std::make_shared<tl::User>(tl::User{u});
}

}  // namespace

TEST(Filters, CommandMatchesNameAndBotSuffix) {
    const auto start = filters::command("start");
    EXPECT_TRUE(start(make_text("/start")));
    EXPECT_TRUE(start(make_text("/start hello")));
    EXPECT_TRUE(start(make_text("/start@mybot hello")));
    EXPECT_FALSE(start(make_text("/startx")));
    EXPECT_FALSE(start(make_text("start")));
}

TEST(Filters, CombinatorsAndRegex) {
    const auto f = filters::text() && !filters::outgoing();
    Message in = make_text("hello");
    EXPECT_TRUE(f(in));
    in.outgoing = true;
    EXPECT_FALSE(f(in));
    EXPECT_TRUE((filters::from_user(5) || filters::command("x"))(make_text("/x")));
    EXPECT_TRUE(filters::regex("^ab[0-9]+$")(make_text("ab123")));
    EXPECT_FALSE(filters::regex("^ab[0-9]+$")(make_text("ab12x")));
}

TEST(Dispatch, ShortMessageReachesHandlerWithFields) {
    Client client(ClientOptions{});
    std::vector<Message> seen;
    client.on_message(filters::all(), [&](Client&, const Message& m) { seen.push_back(m); });

    ty::updateShortMessage s;
    s.id = 42;
    s.user_id = 77;
    s.message = "/start now";
    s.pts = 1;
    s.pts_count = 1;
    s.date = 1700000000;
    s.out = false;
    TlWriter w;
    s.serialize(w);
    client.dispatch_raw_update(bytes_of(w));

    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].id, 42);
    EXPECT_EQ(seen[0].text, "/start now");
    EXPECT_EQ(seen[0].chat.kind, PeerKind::User);
    EXPECT_EQ(seen[0].chat.id, 77);
    EXPECT_FALSE(seen[0].outgoing);
    EXPECT_TRUE(filters::command("start")(seen[0]));
}

TEST(Dispatch, ShortChatMessageUsesChatPeer) {
    Client client(ClientOptions{});
    std::vector<Message> seen;
    client.on_message(filters::group(), [&](Client&, const Message& m) { seen.push_back(m); });

    ty::updateShortChatMessage s;
    s.id = 3;
    s.from_id = 11;
    s.chat_id = 500;
    s.message = "hi group";
    s.pts = 1;
    s.pts_count = 1;
    s.date = 1;
    TlWriter w;
    s.serialize(w);
    client.dispatch_raw_update(bytes_of(w));

    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].chat.kind, PeerKind::Chat);
    EXPECT_EQ(seen[0].chat.id, 500);
    ASSERT_TRUE(seen[0].sender.has_value());
    EXPECT_EQ(seen[0].sender->id, 11);
}

TEST(Dispatch, UpdatesContainerCachesPeersAndDeliversMessage) {
    Client client(ClientOptions{});
    std::vector<Message> seen;
    client.on_message(filters::all(), [&](Client&, const Message& m) { seen.push_back(m); });

    ty::message m;
    m.id = 9;
    m.peer_id = std::make_shared<tl::Peer>(tl::Peer{ty::peerUser{77}});
    m.from_id = std::make_shared<tl::Peer>(tl::Peer{ty::peerUser{77}});
    m.date = 5;
    m.message = "via container";
    m.out = false;

    ty::updateNewMessage nm;
    nm.message = std::make_shared<tl::Message>(tl::Message{m});
    nm.pts = 1;
    nm.pts_count = 1;

    ty::updates container;
    container.updates.push_back(std::make_shared<tl::Update>(tl::Update{nm}));
    container.users.push_back(user_obj(77, 123456789, "Alice"));
    container.date = 5;
    container.seq = 0;
    TlWriter w;
    container.serialize(w);
    client.dispatch_raw_update(bytes_of(w));

    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].text, "via container");
    EXPECT_EQ(seen[0].chat.id, 77);
    EXPECT_EQ(seen[0].chat.access_hash, 123456789);
    EXPECT_TRUE(seen[0].chat.has_access_hash);

    // The cached access hash lets us address the user without resolving again.
    const tl::InputPeer peer = client.input_peer(seen[0].chat);
    const auto* up = std::get_if<ty::inputPeerUser>(&peer.value);
    ASSERT_NE(up, nullptr);
    EXPECT_EQ(up->access_hash, 123456789);
}

TEST(Dispatch, HandlerExceptionDoesNotStopOthers) {
    Client client(ClientOptions{});
    int calls = 0;
    client.on_message(filters::all(), [&](Client&, const Message&) { throw std::runtime_error("boom"); });
    client.on_message(filters::all(), [&](Client&, const Message&) { ++calls; });

    ty::updateShortMessage s;
    s.id = 1;
    s.user_id = 2;
    s.message = "x";
    s.pts = 1;
    s.pts_count = 1;
    s.date = 1;
    TlWriter w;
    s.serialize(w);
    client.dispatch_raw_update(bytes_of(w));
    EXPECT_EQ(calls, 1);
}

TEST(Convert, DocumentMediaCarriesFileNameAndLocation) {
    ty::document d;
    d.id = 8001;
    d.access_hash = 4242;
    d.file_reference = Bytes{1, 2, 3};
    d.date = 1;
    d.mime_type = "application/pdf";
    d.size = 2048;
    d.dc_id = 2;
    d.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{"report.pdf"}}));

    ty::messageMediaDocument media;
    media.document = std::make_shared<tl::Document>(tl::Document{d});
    const tl::MessageMedia mm{media};
    const MediaInfo info = detail::media_from_tl(mm);
    EXPECT_EQ(info.kind, MediaKind::Document);
    EXPECT_EQ(info.id, 8001);
    EXPECT_EQ(info.access_hash, 4242);
    EXPECT_EQ(info.file_name, "report.pdf");
    EXPECT_EQ(info.mime_type, "application/pdf");
    EXPECT_EQ(info.size, 2048);
}

TEST(Client, PeerWithoutAccessHashIsRejected) {
    Client client(ClientOptions{});
    PeerRef p;
    p.kind = PeerKind::Channel;
    p.id = 5;
    EXPECT_THROW(client.input_peer(p), std::runtime_error);
    p.kind = PeerKind::Chat;
    EXPECT_NO_THROW(client.input_peer(p));
}

TEST(RpcErrors, FloodWaitIsTyped) {
    try {
        cppgram::mtproto::throw_rpc_error(420, "FLOOD_WAIT_37");
        FAIL() << "expected throw";
    } catch (const cppgram::mtproto::FloodWaitError& e) {
        EXPECT_EQ(e.seconds(), 37);
        EXPECT_EQ(e.code(), 420);
    }
}

TEST(RpcErrors, OtherErrorsStayGeneric) {
    try {
        cppgram::mtproto::throw_rpc_error(400, "USER_NOT_PARTICIPANT");
        FAIL() << "expected throw";
    } catch (const cppgram::mtproto::FloodWaitError&) {
        FAIL() << "should not be flood wait";
    } catch (const cppgram::mtproto::RpcError& e) {
        EXPECT_EQ(e.code(), 400);
    }
}

TEST(Members, AdminParticipantConverts) {
    cppgram::detail::PeerCache cache;
    cppgram::mtproto::tl::types::channelParticipantAdmin admin;
    admin.user_id = 42;
    admin.promoted_by = 1;
    admin.date = 0;
    admin.can_edit = true;
    const cppgram::mtproto::tl::ChannelParticipant part{admin};
    const auto member = cppgram::detail::chat_member_from_tl(part, cache);
    ASSERT_TRUE(member.has_value());
    EXPECT_EQ(member->user.id, 42);
    EXPECT_EQ(member->status, cppgram::MemberStatus::Admin);
    EXPECT_TRUE(member->can_edit);
}

TEST(Members, LeftParticipantConverts) {
    cppgram::detail::PeerCache cache;
    cppgram::mtproto::tl::types::channelParticipantLeft left;
    left.peer = std::make_shared<cppgram::mtproto::tl::Peer>(
        cppgram::mtproto::tl::Peer{cppgram::mtproto::tl::types::peerUser{7}});
    const cppgram::mtproto::tl::ChannelParticipant part{left};
    const auto member = cppgram::detail::chat_member_from_tl(part, cache);
    ASSERT_TRUE(member.has_value());
    EXPECT_EQ(member->user.id, 7);
    EXPECT_EQ(member->status, cppgram::MemberStatus::Left);
}

TEST(FiltersTyped, CallbackPrefixAndInlineRegex) {
    cppgram::CallbackQuery q;
    q.data = "buy:42";
    EXPECT_TRUE(cppgram::filters::callback_data_prefix("buy:")(q));
    EXPECT_FALSE(cppgram::filters::callback_data_prefix("sell:")(q));
    EXPECT_TRUE(cppgram::filters::callback_regex("^buy:[0-9]+$")(q));

    cppgram::InlineQuery iq;
    iq.query = "cats and dogs";
    EXPECT_TRUE(cppgram::filters::inline_regex("dogs$")(iq));
    EXPECT_FALSE(cppgram::filters::inline_regex("^dogs")(iq));
}

TEST(Polls, PollMediaParsesQuestionOptionsAndVotes) {
    namespace tl = cppgram::mtproto::tl;
    namespace ty = cppgram::mtproto::tl::types;

    ty::poll p;
    p.id = 77;
    p.question = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{"Best?", {}}});
    for (const char* label : {"A", "B"}) {
        ty::pollAnswer a;
        a.text = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{label, {}}});
        a.option = cppgram::mtproto::Bytes{static_cast<std::uint8_t>(label[0] == 'A' ? '0' : '1')};
        p.answers.push_back(std::make_shared<tl::PollAnswer>(tl::PollAnswer{a}));
    }
    ty::pollAnswerVoters v;
    v.option = cppgram::mtproto::Bytes{'1'};
    v.voters = 5;
    v.chosen = true;
    ty::pollResults results;
    results.results = std::vector<std::shared_ptr<tl::PollAnswerVoters>>{
        std::make_shared<tl::PollAnswerVoters>(tl::PollAnswerVoters{v})};

    ty::messageMediaPoll media;
    media.poll = std::make_shared<tl::Poll>(tl::Poll{p});
    media.results = std::make_shared<tl::PollResults>(tl::PollResults{results});
    const tl::MessageMedia mm{media};

    const auto info = cppgram::detail::poll_from_tl(mm);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->question, "Best?");
    ASSERT_EQ(info->options.size(), 2u);
    EXPECT_EQ(info->options[0].text, "A");
    EXPECT_EQ(info->options[0].voters, 0);
    EXPECT_EQ(info->options[1].voters, 5);
    EXPECT_TRUE(info->options[1].chosen);
    EXPECT_EQ(info->options[1].option_id, "1");
    EXPECT_EQ(cppgram::detail::media_from_tl(mm).kind, cppgram::MediaKind::Poll);
}

TEST(Stickers, StickerAttributeParsesEmojiAndSetId) {
    namespace tl = cppgram::mtproto::tl;
    namespace ty = cppgram::mtproto::tl::types;

    ty::documentAttributeSticker attr;
    attr.alt = "🔥";
    attr.stickerset = std::make_shared<tl::InputStickerSet>(
        tl::InputStickerSet{ty::inputStickerSetID{555, 666}});
    ty::document doc;
    doc.id = 9;
    doc.access_hash = 10;
    doc.mime_type = "image/webp";
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{attr}));
    ty::messageMediaDocument media;
    media.document = std::make_shared<tl::Document>(tl::Document{doc});
    const tl::MessageMedia mm{media};

    const auto st = cppgram::detail::sticker_from_tl(mm);
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->emoji, "🔥");
    EXPECT_EQ(st->set_id, 555);
    EXPECT_EQ(st->set_access_hash, 666);
    EXPECT_EQ(st->media.kind, cppgram::MediaKind::Document);
    EXPECT_TRUE(st->media.is_sticker);
}
