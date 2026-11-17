// Request emission for the remaining Kurigram-invoked methods. Each entry calls one Client method against a fake DC
// that answers every request with rpc_error. The test checks that the method's constructor id was sent. It does not
// check the reply, so it works for every response shape. Offline only. NOT RUN in this session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::AdminLogFilterInfo;
using cppgram::BirthdayInfo;
using cppgram::BotAppRef;
using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::DocumentRef;
using cppgram::EmailVerifyPurposeInfo;
using cppgram::EmojiStatusInfo;
using cppgram::GroupCallRef;
using cppgram::InlineMessageRef;
using cppgram::InlineTextResultInfo;
using cppgram::InvoiceRef;
using cppgram::JoinChatBotResultInfo;
using cppgram::NotifySettingsInfo;
using cppgram::PaidReactionPrivacyInfo;
using cppgram::PeerColorInfo;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::PollAnswerInfo;
using cppgram::ProfileTab;
using cppgram::SavedStarGiftRef;
using cppgram::ShippingOptionInfo;
using cppgram::StarsAmountInfo;
using cppgram::StickerSetRef;
using cppgram::TakeoutOptions;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kRpcErrorId = 0x2144ca19u;  // rpc_error: error_code int, error_message string

// account.password with a 2FA password set. The client builds an SRP check from it before the 2FA-gated methods
// send their request, so the fixture needs a 256-byte modulus, both salts, g, the SRP id and B. These values are
// fixture data, not any account's state.
void write_password_with_2fa(cppgram::mtproto::TlWriter& out) {
    namespace tl = cppgram::mtproto::tl;
    namespace ty = cppgram::mtproto::tl::types;
    ty::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow algo;
    algo.salt1 = {0x01, 0x02, 0x03, 0x04};
    algo.salt2 = {0x05, 0x06};
    algo.g = 3;
    cppgram::mtproto::Bytes p(256, 0xff);
    p.back() = 0xfd;
    algo.p = p;
    ty::account_password pw;
    pw.has_password = true;
    pw.current_algo = std::make_shared<tl::PasswordKdfAlgo>(tl::PasswordKdfAlgo{algo});
    pw.srp_id = 4242;
    pw.srp_B = cppgram::mtproto::Bytes(256, 0x02);
    pw.new_algo = std::make_shared<tl::PasswordKdfAlgo>(tl::PasswordKdfAlgo{algo});
    pw.new_secure_algo = std::make_shared<tl::SecurePasswordKdfAlgo>(
        tl::SecurePasswordKdfAlgo{ty::securePasswordKdfAlgoUnknown{}});
    pw.secure_random = cppgram::mtproto::Bytes{0x00};
    pw.serialize(out);
}

std::string session_path() {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-remaining-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + ".session"))
        .string();
}

PeerRef user(std::int64_t id) {
    PeerRef p;
    p.kind = PeerKind::User;
    p.id = id;
    p.access_hash = id * 3 + 1;
    p.has_access_hash = true;
    return p;
}

PeerRef channel(std::int64_t id) {
    PeerRef p;
    p.kind = PeerKind::Channel;
    p.id = id;
    p.access_hash = id * 5 + 2;
    p.has_access_hash = true;
    return p;
}

PeerRef chat(std::int64_t id) {
    PeerRef p;
    p.kind = PeerKind::Chat;
    p.id = id;
    return p;
}

DocumentRef doc(std::int64_t id) {
    DocumentRef d;
    d.id = id;
    d.access_hash = id + 9;
    d.file_reference = {0x01, 0x02};
    return d;
}

struct Case {
    const char* name;
    std::uint32_t expected_id;
    std::function<void(Client&)> call;
};

}  // namespace

TEST(RemainingMethodsFakeDc, EachMethodSendsItsConstructorId) {
    std::set<std::uint32_t> seen;
    std::atomic<bool> armed{false};  // the connect handshake is served normally; errors start after connect
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (!armed.load()) return false;
        if (id == fn::account_getPassword::kId) {
            write_password_with_2fa(out);  // lets the 2FA-gated methods reach their own request
            return true;
        }
        seen.insert(id);
        out.write_uint32(kRpcErrorId);
        out.write_int32(400);
        out.write_string("TEST_ERROR");
        return true;
    });

    const std::string path = session_path();
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = path;
    o.connect_timeout_ms = 2000;
    Client client(o);
    client.connect();
    armed = true;

    StarsAmountInfo amount;
    amount.amount = 100;
    StickerSetRef set;
    set.kind = StickerSetRef::Kind::ShortName;
    set.short_name = "pack";
    InvoiceRef slug_invoice;
    slug_invoice.slug = "invoice-slug";
    InlineTextResultInfo text_result;
    text_result.id = "r1";
    text_result.type = "article";
    text_result.text = "hi";
    SavedStarGiftRef slug_gift;
    slug_gift.kind = SavedStarGiftRef::Kind::Slug;
    slug_gift.slug = "gift-slug";
    BirthdayInfo bday;
    bday.day = 3;
    bday.month = 4;

    const std::vector<Case> cases = {
        // account.*
        {"change_phone", fn::account_changePhone::kId,
         [](Client& c) { c.change_phone("+15550001", "hash", "12345"); }},
        {"send_change_phone_code", fn::account_sendChangePhoneCode::kId,
         [](Client& c) { c.send_change_phone_code("+15550001"); }},
        {"send_verify_phone_code", fn::account_sendVerifyPhoneCode::kId,
         [](Client& c) { c.send_verify_phone_code("+15550001"); }},
        {"check_paid_auth", fn::auth_checkPaidAuth::kId,
         [](Client& c) { c.check_paid_auth("+15550001", "hash", 99); }},
        {"send_verify_email_code", fn::account_sendVerifyEmailCode::kId,
         [](Client& c) {
             EmailVerifyPurposeInfo p;
             p.kind = EmailVerifyPurposeInfo::Kind::LoginChange;
             c.send_verify_email_code(p, "a@example.com");
         }},
        {"verify_email", fn::account_verifyEmail::kId,
         [](Client& c) {
             EmailVerifyPurposeInfo p;
             p.kind = EmailVerifyPurposeInfo::Kind::LoginChange;
             cppgram::EmailVerificationInfo v;
             v.value = "123456";
             c.verify_email(p, v);
         }},
        {"set_authorization_ttl", fn::account_setAuthorizationTTL::kId, [](Client& c) { c.set_authorization_ttl(30); }},
        {"set_main_profile_tab", fn::account_setMainProfileTab::kId,
         [](Client& c) { c.set_main_profile_tab(ProfileTab::Gifts); }},
        {"update_birthday", fn::account_updateBirthday::kId, [bday](Client& c) { c.update_birthday(bday); }},
        {"update_color", fn::account_updateColor::kId,
         [](Client& c) {
             PeerColorInfo col;
             col.color = 3;
             c.update_color(true, col);
         }},
        {"update_emoji_status", fn::account_updateEmojiStatus::kId,
         [](Client& c) {
             EmojiStatusInfo s;
             s.kind = EmojiStatusInfo::Kind::Emoji;
             s.document_id = 77;
             c.update_emoji_status(s);
         }},
        {"update_notify_settings", fn::account_updateNotifySettings::kId,
         [](Client& c) {
             NotifySettingsInfo s;
             s.silent = true;
             c.update_notify_settings(user(555), 0, s);
         }},
        {"update_personal_channel", fn::account_updatePersonalChannel::kId,
         [](Client& c) { c.update_personal_channel(channel(777)); }},
        {"save_music", fn::account_saveMusic::kId, [](Client& c) { c.save_music(doc(10)); }},
        {"init_takeout_session", fn::account_initTakeoutSession::kId,
         [](Client& c) {
             TakeoutOptions t;
             t.contacts = true;
             c.init_takeout_session(t);
         }},
        {"finish_takeout_session", fn::account_finishTakeoutSession::kId,
         [](Client& c) { c.finish_takeout_session(true); }},
        {"get_tmp_password", fn::account_getTmpPassword::kId, [](Client& c) { c.get_tmp_password("pw", 60); }},
        {"reset_authorizations", fn::auth_resetAuthorizations::kId, [](Client& c) { c.reset_authorizations(); }},
        // auth/business/chatlists
        {"get_bot_business_connection", fn::account_getBotBusinessConnection::kId,
         [](Client& c) { c.get_bot_business_connection("conn-1"); }},
        {"leave_chatlist", fn::chatlists_leaveChatlist::kId, [](Client& c) { c.leave_chatlist(4, {user(555)}); }},
        // bots.*
        {"check_bot_username", fn::bots_checkUsername::kId, [](Client& c) { c.check_bot_username("some_bot"); }},
        {"create_bot", fn::bots_createBot::kId,
         [](Client& c) { c.create_bot("Name", "some_bot", user(555)); }},
        {"edit_bot_access_settings", fn::bots_editAccessSettings::kId,
         [](Client& c) { c.edit_bot_access_settings(user(555), true, {user(556)}); }},
        {"export_bot_token", fn::bots_exportBotToken::kId, [](Client& c) { c.export_bot_token(user(555), true); }},
        {"get_bot_access_settings", fn::bots_getAccessSettings::kId,
         [](Client& c) { c.get_bot_access_settings(user(555)); }},
        {"get_admined_bots", fn::bots_getAdminedBots::kId, [](Client& c) { c.get_admined_bots(); }},
        {"get_bot_menu_button", fn::bots_getBotMenuButton::kId, [](Client& c) { c.get_bot_menu_button(user(555)); }},
        {"set_join_chat_results", fn::bots_setJoinChatResults::kId,
         [](Client& c) {
             JoinChatBotResultInfo r;
             r.kind = JoinChatBotResultInfo::Kind::Queued;
             c.set_join_chat_results(12, r);
         }},
        // channels.*
        {"check_channel_username", fn::channels_checkUsername::kId,
         [](Client& c) { c.check_channel_username(channel(777), "chan"); }},
        {"delete_channel", fn::channels_deleteChannel::kId, [](Client& c) { c.delete_channel(channel(777)); }},
        {"delete_channel_history", fn::channels_deleteHistory::kId,
         [](Client& c) { c.delete_channel_history(channel(777), 50, true); }},
        {"delete_participant_history", fn::channels_deleteParticipantHistory::kId,
         [](Client& c) { c.delete_participant_history(channel(777), user(555)); }},
        {"get_admin_log", fn::channels_getAdminLog::kId,
         [](Client& c) {
             AdminLogFilterInfo f;
             f.ban = true;
             c.get_admin_log(channel(777), "", f);
         }},
        {"get_admined_public_channels", fn::channels_getAdminedPublicChannels::kId,
         [](Client& c) { c.get_admined_public_channels(true); }},
        {"get_channel_recommendations", fn::channels_getChannelRecommendations::kId,
         [](Client& c) { c.get_channel_recommendations(channel(777)); }},
        {"get_channels", fn::channels_getChannels::kId, [](Client& c) { c.get_channels({channel(777)}); }},
        {"get_groups_for_discussion", fn::channels_getGroupsForDiscussion::kId,
         [](Client& c) { c.get_groups_for_discussion(); }},
        {"get_send_as", fn::channels_getSendAs::kId, [](Client& c) { c.get_send_as(channel(777)); }},
        {"invite_to_channel", fn::channels_inviteToChannel::kId,
         [](Client& c) { c.invite_to_channel(channel(777), {user(555)}); }},
        {"search_posts", fn::channels_searchPosts::kId,
         [](Client& c) { c.search_posts("tag", "", 0, PeerRef{}, 0, 10); }},
        {"set_channel_main_profile_tab", fn::channels_setMainProfileTab::kId,
         [](Client& c) { c.set_channel_main_profile_tab(channel(777), ProfileTab::Media); }},
        {"update_channel_emoji_status", fn::channels_updateEmojiStatus::kId,
         [](Client& c) {
             EmojiStatusInfo s;
             s.kind = EmojiStatusInfo::Kind::Empty;
             c.update_channel_emoji_status(channel(777), s);
         }},
        {"update_paid_messages_price", fn::channels_updatePaidMessagesPrice::kId,
         [](Client& c) { c.update_paid_messages_price(channel(777), 5); }},
        // contacts / ephemeral / phone / users / stickers
        {"get_top_peers", fn::contacts_getTopPeers::kId, [](Client& c) { c.get_top_peers(true); }},
        {"resolve_phone", fn::contacts_resolvePhone::kId, [](Client& c) { c.resolve_phone("+15550001"); }},
        {"search_contacts", fn::contacts_search::kId, [](Client& c) { c.search_contacts("alice"); }},
        {"ephemeral_delete_message", fn::ephemeral_deleteMessage::kId,
         [](Client& c) { c.ephemeral_delete_message(user(555), 9); }},
        {"ephemeral_edit_message", fn::ephemeral_editMessage::kId,
         [](Client& c) { c.ephemeral_edit_message(user(555), 9, "edited"); }},
        {"ephemeral_send_message", fn::ephemeral_sendMessage::kId,
         [](Client& c) { c.ephemeral_send_message(user(555), "hello"); }},
        {"get_group_call_participants", fn::phone_getGroupParticipants::kId,
         [](Client& c) {
             GroupCallRef g;
             g.id = 1;
             g.access_hash = 2;
             c.get_group_call_participants(g, {user(555)}, {});
         }},
        {"suggest_birthday", fn::users_suggestBirthday::kId, [bday](Client& c) { c.suggest_birthday(user(555), bday); }},
        {"change_sticker", fn::stickers_changeSticker::kId,
         [](Client& c) { c.change_sticker(doc(20), std::string("x")); }},
        {"replace_sticker", fn::stickers_replaceSticker::kId,
         [](Client& c) { c.replace_sticker(doc(20), doc(21), "x"); }},
        {"set_sticker_set_thumb", fn::stickers_setStickerSetThumb::kId,
         [set](Client& c) { c.set_sticker_set_thumb(set, doc(22)); }},
        {"suggest_sticker_short_name", fn::stickers_suggestShortName::kId,
         [](Client& c) { c.suggest_sticker_short_name("My Pack"); }},
        // messages.*
        {"add_chat_user", fn::messages_addChatUser::kId, [](Client& c) { c.add_chat_user(12, user(555)); }},
        {"add_poll_answer", fn::messages_addPollAnswer::kId,
         [](Client& c) {
             PollAnswerInfo a;
             a.text = "yes";
             a.option = {0x01};
             c.add_poll_answer(chat(12), 5, a);
         }},
        {"delete_poll_answer", fn::messages_deletePollAnswer::kId,
         [](Client& c) { c.delete_poll_answer(chat(12), 5, {0x01}); }},
        {"compose_message_with_ai", fn::messages_composeMessageWithAI::kId,
         [](Client& c) { c.compose_message_with_ai("text", true); }},
        {"delete_participant_reaction", fn::messages_deleteParticipantReaction::kId,
         [](Client& c) { c.delete_participant_reaction(chat(12), 5, user(555)); }},
        {"delete_participant_reactions", fn::messages_deleteParticipantReactions::kId,
         [](Client& c) { c.delete_participant_reactions(chat(12), user(555)); }},
        {"delete_saved_history", fn::messages_deleteSavedHistory::kId,
         [](Client& c) { c.delete_saved_history(user(555), 10); }},
        {"edit_chat_about", fn::messages_editChatAbout::kId, [](Client& c) { c.edit_chat_about(chat(12), "about"); }},
        {"edit_chat_creator", fn::messages_editChatCreator::kId,
         [](Client& c) { c.edit_chat_creator(chat(12), user(555), "pw"); }},
        {"edit_chat_participant_rank", fn::messages_editChatParticipantRank::kId,
         [](Client& c) { c.edit_chat_participant_rank(chat(12), user(555), "rank"); }},
        {"get_chats", fn::messages_getChats::kId, [](Client& c) { c.get_chats({12, 13}); }},
        {"get_discussion_message", fn::messages_getDiscussionMessage::kId,
         [](Client& c) { c.get_discussion_message(channel(777), 5); }},
        {"get_inline_bot_results", fn::messages_getInlineBotResults::kId,
         [](Client& c) { c.get_inline_bot_results(user(555), user(556), "q"); }},
        {"get_messages_views", fn::messages_getMessagesViews::kId,
         [](Client& c) { c.get_messages_views(channel(777), {1, 2}, true); }},
        {"get_my_stickers", fn::messages_getMyStickers::kId, [](Client& c) { c.get_my_stickers(); }},
        {"get_peer_dialogs", fn::messages_getPeerDialogs::kId, [](Client& c) { c.get_peer_dialogs({user(555)}); }},
        {"get_peer_settings", fn::messages_getPeerSettings::kId, [](Client& c) { c.get_peer_settings(user(555)); }},
        {"get_personal_channel_history", fn::messages_getPersonalChannelHistory::kId,
         [](Client& c) { c.get_personal_channel_history(user(555)); }},
        {"get_pinned_dialogs", fn::messages_getPinnedDialogs::kId, [](Client& c) { c.get_pinned_dialogs(); }},
        {"get_recent_stickers", fn::messages_getRecentStickers::kId, [](Client& c) { c.get_recent_stickers(); }},
        {"get_saved_dialogs", fn::messages_getSavedDialogs::kId, [](Client& c) { c.get_saved_dialogs(); }},
        {"get_saved_dialogs_by_id", fn::messages_getSavedDialogsByID::kId,
         [](Client& c) { c.get_saved_dialogs_by_id({user(555)}); }},
        {"get_saved_history", fn::messages_getSavedHistory::kId,
         [](Client& c) { c.get_saved_history(user(555)); }},
        {"get_search_counters", fn::messages_getSearchCounters::kId,
         [](Client& c) {
             auto filter = std::make_shared<cppgram::mtproto::tl::MessagesFilter>(
                 cppgram::mtproto::tl::MessagesFilter{cppgram::mtproto::tl::types::inputMessagesFilterEmpty{}});
             c.get_search_counters(chat(12), {filter});
         }},
        {"get_split_ranges", fn::messages_getSplitRanges::kId, [](Client& c) { c.get_split_ranges(); }},
        {"install_sticker_set", fn::messages_installStickerSet::kId,
         [set](Client& c) { c.install_sticker_set(set, false); }},
        {"uninstall_sticker_set", fn::messages_uninstallStickerSet::kId,
         [set](Client& c) { c.uninstall_sticker_set(set); }},
        {"reorder_sticker_sets", fn::messages_reorderStickerSets::kId,
         [](Client& c) { c.reorder_sticker_sets({1, 2}); }},
        {"search_sticker_sets", fn::messages_searchStickerSets::kId,
         [](Client& c) { c.search_sticker_sets("cats"); }},
        {"search_stickers", fn::messages_searchStickers::kId,
         [](Client& c) { c.search_stickers("cats", "🐱"); }},
        {"read_mentions", fn::messages_readMentions::kId, [](Client& c) { c.read_mentions(chat(12)); }},
        {"read_reactions", fn::messages_readReactions::kId, [](Client& c) { c.read_reactions(chat(12)); }},
        {"request_app_web_view", fn::messages_requestAppWebView::kId,
         [](Client& c) {
             BotAppRef app;
             app.id = 1;
             app.access_hash = 2;
             c.request_app_web_view(user(555), app);
         }},
        {"request_main_web_view", fn::messages_requestMainWebView::kId,
         [](Client& c) { c.request_main_web_view(user(555), user(556)); }},
        {"request_simple_web_view", fn::messages_requestSimpleWebView::kId,
         [](Client& c) { c.request_simple_web_view(user(556), "https://example.com/app"); }},
        {"request_web_view", fn::messages_requestWebView::kId,
         [](Client& c) { c.request_web_view(user(555), user(556), "https://example.com/app"); }},
        {"save_default_send_as", fn::messages_saveDefaultSendAs::kId,
         [](Client& c) { c.save_default_send_as(chat(12), channel(777)); }},
        {"save_gif", fn::messages_saveGif::kId, [](Client& c) { c.save_gif(doc(30)); }},
        {"send_inline_bot_result", fn::messages_sendInlineBotResult::kId,
         [](Client& c) { c.send_inline_bot_result(chat(12), 9, "r1"); }},
        {"send_paid_reaction", fn::messages_sendPaidReaction::kId,
         [](Client& c) {
             PaidReactionPrivacyInfo p;
             p.kind = PaidReactionPrivacyInfo::Kind::Anonymous;
             c.send_paid_reaction(channel(777), 5, 2, p);
         }},
        {"send_screenshot_notification", fn::messages_sendScreenshotNotification::kId,
         [](Client& c) { c.send_screenshot_notification(user(555), 5); }},
        {"send_web_view_result_message", fn::messages_sendWebViewResultMessage::kId,
         [text_result](Client& c) { c.send_web_view_result_message("query-1", text_result); }},
        {"set_bot_guest_chat_result", fn::messages_setBotGuestChatResult::kId,
         [text_result](Client& c) { c.set_bot_guest_chat_result(9, text_result); }},
        {"set_bot_shipping_results", fn::messages_setBotShippingResults::kId,
         [](Client& c) {
             ShippingOptionInfo o;
             o.id = "ship";
             o.title = "Post";
             o.prices = {cppgram::LabeledPriceInfo{"Fee", 100}};
             c.set_bot_shipping_results(9, std::nullopt, {o});
         }},
        {"set_inline_game_score", fn::messages_setInlineGameScore::kId,
         [](Client& c) {
             InlineMessageRef m;
             m.id = 4;
             m.access_hash = 5;
             c.set_inline_game_score(m, user(555), 42);
         }},
        {"start_bot", fn::messages_startBot::kId, [](Client& c) { c.start_bot(user(556), user(555), "start"); }},
        {"summarize_text", fn::messages_summarizeText::kId, [](Client& c) { c.summarize_text(chat(12), 5); }},
        {"toggle_no_forwards", fn::messages_toggleNoForwards::kId,
         [](Client& c) { c.toggle_no_forwards(chat(12), true); }},
        {"toggle_suggested_post_approval", fn::messages_toggleSuggestedPostApproval::kId,
         [](Client& c) { c.toggle_suggested_post_approval(channel(777), 5, true); }},
        // payments.* (wrapped here for emission only)
        {"get_payment_form", fn::payments_getPaymentForm::kId, [slug_invoice](Client& c) { c.get_payment_form(slug_invoice); }},
        {"send_stars_form", fn::payments_sendStarsForm::kId,
         [slug_invoice](Client& c) { c.send_stars_form(3, slug_invoice); }},
        {"check_gift_code", fn::payments_checkGiftCode::kId, [](Client& c) { c.check_gift_code("code"); }},
        {"fulfill_stars_subscription", fn::payments_fulfillStarsSubscription::kId,
         [](Client& c) { c.fulfill_stars_subscription(user(555), "sub"); }},
        {"change_stars_subscription", fn::payments_changeStarsSubscription::kId,
         [](Client& c) { c.change_stars_subscription(user(555), "sub", true); }},
        {"craft_star_gift", fn::payments_craftStarGift::kId, [slug_gift](Client& c) { c.craft_star_gift({slug_gift}); }},
        {"delete_star_gift", fn::payments_deleteStarGift::kId, [slug_gift](Client& c) { c.delete_star_gift(slug_gift); }},
        {"delete_star_gift_collection", fn::payments_deleteStarGiftCollection::kId,
         [](Client& c) { c.delete_star_gift_collection(user(555), 2); }},
        {"reorder_star_gift_collections", fn::payments_reorderStarGiftCollections::kId,
         [](Client& c) { c.reorder_star_gift_collections(user(555), {1, 2}); }},
        {"toggle_star_gifts_pinned_to_top", fn::payments_toggleStarGiftsPinnedToTop::kId,
         [slug_gift](Client& c) { c.toggle_star_gifts_pinned_to_top(user(555), {slug_gift}); }},
        {"update_star_gift_price", fn::payments_updateStarGiftPrice::kId,
         [slug_gift, amount](Client& c) { c.update_star_gift_price(slug_gift, amount); }},
        {"send_star_gift_offer", fn::payments_sendStarGiftOffer::kId,
         [amount](Client& c) { c.send_star_gift_offer(user(555), "gift-slug", amount, 3600); }},
        {"resolve_star_gift_offer", fn::payments_resolveStarGiftOffer::kId,
         [](Client& c) { c.resolve_star_gift_offer(5, true); }},
        {"refund_stars_charge", fn::payments_refundStarsCharge::kId,
         [](Client& c) { c.refund_stars_charge(user(555), "charge"); }},
        {"get_craft_star_gifts", fn::payments_getCraftStarGifts::kId,
         [](Client& c) { c.get_craft_star_gifts(1); }},
        {"get_resale_star_gifts", fn::payments_getResaleStarGifts::kId,
         [](Client& c) { c.get_resale_star_gifts(1); }},
        {"get_star_gift_auction_state", fn::payments_getStarGiftAuctionState::kId,
         [](Client& c) {
             cppgram::StarGiftAuctionRef a;
             a.gift_id = 1;
             c.get_star_gift_auction_state(a, 0);
         }},
        {"get_star_gift_upgrade_attributes", fn::payments_getStarGiftUpgradeAttributes::kId,
         [](Client& c) { c.get_star_gift_upgrade_attributes(1); }},
        {"get_unique_star_gift_value_info", fn::payments_getUniqueStarGiftValueInfo::kId,
         [](Client& c) { c.get_unique_star_gift_value_info("slug"); }},
        {"export_invoice", fn::payments_exportInvoice::kId,
         [](Client& c) {
             auto media = std::make_shared<cppgram::mtproto::tl::InputMedia>(
                 cppgram::mtproto::tl::InputMedia{cppgram::mtproto::tl::types::inputMediaEmpty{}});
             c.export_invoice(media);
         }},
        // wallet.*
        {"get_wallet_proof_challenge", fn::wallet_getProofChallenge::kId,
         [](Client& c) { c.get_wallet_proof_challenge(); }},
        {"get_wallet_user_addresses", fn::wallet_getUserAddresses::kId,
         [](Client& c) { c.get_wallet_user_addresses({user(555)}, {"EQ-test"}); }},
        {"replace_wallet", fn::wallet_replaceWallet::kId,
         [](Client& c) {
             cppgram::WalletReplacement r;
             r.kind = cppgram::WalletReplacement::Kind::New;
             c.replace_wallet(r);
         }},
        // premium / saved music / app config / transport
        {"get_my_boosts", fn::premium_getMyBoosts::kId, [](Client& c) { c.get_my_boosts(); }},
        {"get_app_config", fn::help_getAppConfig::kId, [](Client& c) { c.get_app_config(0); }},
        {"get_saved_music", fn::users_getSavedMusic::kId, [](Client& c) { c.get_saved_music(user(555)); }},
    };

    std::vector<std::string> failures;
    for (const auto& test_case : cases) {
        seen.clear();
        try {
            test_case.call(client);
        } catch (const std::exception&) {
            // Expected: the fake DC answers every request with an RPC error. Exceptions thrown before the request
            // is sent (argument validation) leave the id unseen and are reported below.
        }
        if (seen.count(test_case.expected_id) == 0) failures.push_back(test_case.name);
    }
    client.disconnect();
    std::filesystem::remove(path);
    for (const auto& name : failures) ADD_FAILURE() << "constructor id not sent by: " << name;
    EXPECT_TRUE(failures.empty());
}
