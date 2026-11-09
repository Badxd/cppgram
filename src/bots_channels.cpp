// bots.* and channels.* calls, plus forum topic pinning (messages.updatePinnedForumTopic).
#include "cppgram/client.h"

#include <stdexcept>
#include <type_traits>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputUser> input_user_of(const PeerRef& user) {
    if (user.kind != PeerKind::User || !user.has_access_hash) {
        throw std::invalid_argument("expected a user peer with an access hash");
    }
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
}

std::shared_ptr<tl::InputChannel> input_channel_of(const PeerRef& channel) {
    if (channel.kind != PeerKind::Channel || !channel.has_access_hash) {
        throw std::invalid_argument("expected a channel peer with an access hash");
    }
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{channel.id, channel.access_hash}});
}

std::shared_ptr<tl::JoinChatBotResult> join_result_of(const JoinChatBotResultInfo& r) {
    switch (r.kind) {
        case JoinChatBotResultInfo::Kind::Approved:
            return std::make_shared<tl::JoinChatBotResult>(tl::JoinChatBotResult{ty::joinChatBotResultApproved{}});
        case JoinChatBotResultInfo::Kind::Declined:
            return std::make_shared<tl::JoinChatBotResult>(tl::JoinChatBotResult{ty::joinChatBotResultDeclined{}});
        case JoinChatBotResultInfo::Kind::Queued:
            return std::make_shared<tl::JoinChatBotResult>(tl::JoinChatBotResult{ty::joinChatBotResultQueued{}});
        case JoinChatBotResultInfo::Kind::WebView: {
            if (r.url.empty()) throw std::invalid_argument("WebView join result needs a url");
            ty::joinChatBotResultWebView w;
            w.url = r.url;
            return std::make_shared<tl::JoinChatBotResult>(tl::JoinChatBotResult{w});
        }
    }
    throw std::invalid_argument("unknown join chat result");
}

}  // namespace

// ---- bots

bool Client::check_bot_username(const std::string& username, bool additional) {
    require_session();
    fn::bots_checkUsername req;
    req.username = username;
    if (additional) req.additional = true;
    return call_rpc(req);
}

std::int64_t Client::create_bot(const std::string& name, const std::string& username, const PeerRef& manager,
                                bool via_deeplink) {
    require_session();
    fn::bots_createBot req;
    req.name = name;
    req.username = username;
    req.manager_id = input_user_of(manager);
    if (via_deeplink) req.via_deeplink = true;
    const tl::User user = call_rpc(req);
    const auto* u = std::get_if<ty::user>(&user.value);
    if (u == nullptr) throw std::runtime_error("bots.createBot returned no user");
    return u->id;
}

bool Client::edit_bot_access_settings(const PeerRef& bot, bool restricted, const std::vector<PeerRef>& add_users) {
    require_session();
    fn::bots_editAccessSettings req;
    req.bot = input_user_of(bot);
    if (restricted) req.restricted = true;
    if (!add_users.empty()) {
        std::vector<std::shared_ptr<tl::InputUser>> users;
        for (const auto& u : add_users) users.push_back(input_user_of(u));
        req.add_users = std::move(users);
    }
    return call_rpc(req);
}

std::string Client::export_bot_token(const PeerRef& bot, bool revoke) {
    require_session();
    fn::bots_exportBotToken req;
    req.bot = input_user_of(bot);
    req.revoke = revoke;
    const tl::bots_ExportedBotToken result = call_rpc(req);
    const auto* t = std::get_if<ty::bots_exportedBotToken>(&result.value);
    if (t == nullptr) throw std::runtime_error("unexpected bots.ExportedBotToken variant");
    return t->token;
}

BotAccessSettingsInfo Client::get_bot_access_settings(const PeerRef& bot) {
    require_session();
    fn::bots_getAccessSettings req;
    req.bot = input_user_of(bot);
    const tl::bots_AccessSettings result = call_rpc(req);
    const auto* a = std::get_if<ty::bots_accessSettings>(&result.value);
    if (a == nullptr) throw std::runtime_error("unexpected bots.AccessSettings variant");
    BotAccessSettingsInfo out;
    out.restricted = a->restricted;
    if (a->add_users) {
        cache_users_chats(*a->add_users, {});
        for (const auto& u : *a->add_users) {
            if (!u) continue;
            if (const auto* user = std::get_if<ty::user>(&u->value)) out.added_user_ids.push_back(user->id);
        }
    }
    return out;
}

std::vector<PeerRef> Client::get_admined_bots() {
    require_session();
    fn::bots_getAdminedBots req;
    const std::vector<std::shared_ptr<tl::User>> users = call_rpc(req);
    cache_users_chats(users, {});
    std::vector<PeerRef> out;
    for (const auto& u : users) {
        if (!u) continue;
        if (const auto* user = std::get_if<ty::user>(&u->value)) {
            out.push_back(detail::peer_from_tl(tl::Peer{ty::peerUser{user->id}}, cache_));
        }
    }
    return out;
}

BotMenuButtonInfo Client::get_bot_menu_button(const PeerRef& user) {
    require_session();
    fn::bots_getBotMenuButton req;
    req.user_id = input_user_of(user);
    const tl::BotMenuButton button = call_rpc(req);
    BotMenuButtonInfo out;
    if (std::holds_alternative<ty::botMenuButtonCommands>(button.value)) {
        out.kind = BotMenuButtonInfo::Kind::Commands;
    } else if (const auto* web = std::get_if<ty::botMenuButton>(&button.value)) {
        out.kind = BotMenuButtonInfo::Kind::Web;
        out.text = web->text;
        out.url = web->url;
    } else {
        out.kind = BotMenuButtonInfo::Kind::Default;
    }
    return out;
}

bool Client::set_join_chat_results(std::int64_t query_id, const JoinChatBotResultInfo& result) {
    require_session();
    fn::bots_setJoinChatResults req;
    req.query_id = query_id;
    req.result = join_result_of(result);
    return call_rpc(req);
}

std::vector<Chat> Client::chat_list(const tl::messages_Chats& result) {
    const std::vector<std::shared_ptr<tl::Chat>>* chats = nullptr;
    if (const auto* full = std::get_if<ty::messages_chats>(&result.value)) {
        chats = &full->chats;
    } else if (const auto* slice = std::get_if<ty::messages_chatsSlice>(&result.value)) {
        chats = &slice->chats;
    } else {
        throw std::runtime_error("unexpected messages.Chats variant");
    }
    cache_users_chats({}, *chats);
    std::vector<Chat> out;
    for (const auto& c : *chats) {
        if (!c) continue;
        if (auto chat = detail::chat_from_tl(*c)) out.push_back(std::move(*chat));
    }
    return out;
}

std::vector<PeerRef> Client::chats_as_peers(const tl::messages_Chats& result) {
    const auto* chats = std::get_if<ty::messages_chats>(&result.value);
    if (chats == nullptr) {
        const auto* slice = std::get_if<ty::messages_chatsSlice>(&result.value);
        if (slice == nullptr) throw std::runtime_error("unexpected messages.Chats variant");
        cache_users_chats({}, slice->chats);
        std::vector<PeerRef> out;
        for (const auto& c : slice->chats) {
            if (!c) continue;
            if (const auto* ch = std::get_if<ty::channel>(&c->value)) out.push_back(detail::peer_from_tl(tl::Peer{ty::peerChannel{ch->id}}, cache_));
            else if (const auto* ct = std::get_if<ty::chat>(&c->value)) out.push_back(detail::peer_from_tl(tl::Peer{ty::peerChat{ct->id}}, cache_));
        }
        return out;
    }
    cache_users_chats({}, chats->chats);
    std::vector<PeerRef> out;
    for (const auto& c : chats->chats) {
        if (!c) continue;
        if (const auto* ch = std::get_if<ty::channel>(&c->value)) out.push_back(detail::peer_from_tl(tl::Peer{ty::peerChannel{ch->id}}, cache_));
        else if (const auto* ct = std::get_if<ty::chat>(&c->value)) out.push_back(detail::peer_from_tl(tl::Peer{ty::peerChat{ct->id}}, cache_));
    }
    return out;
}

// ---- channels

bool Client::check_channel_username(const PeerRef& channel, const std::string& username) {
    require_session();
    fn::channels_checkUsername req;
    req.channel = input_channel_of(channel);
    req.username = username;
    return call_rpc(req);
}

bool Client::delete_channel(const PeerRef& channel) {
    require_session();
    fn::channels_deleteChannel req;
    req.channel = input_channel_of(channel);
    (void)call_rpc(req);
    return true;
}

bool Client::delete_channel_history(const PeerRef& channel, std::int32_t max_id, bool for_everyone) {
    require_session();
    fn::channels_deleteHistory req;
    req.channel = input_channel_of(channel);
    req.max_id = max_id;
    if (for_everyone) req.for_everyone = true;
    (void)call_rpc(req);
    return true;
}

AffectedHistoryInfo Client::delete_participant_history(const PeerRef& channel, const PeerRef& participant) {
    require_session();
    fn::channels_deleteParticipantHistory req;
    req.channel = input_channel_of(channel);
    req.participant = std::make_shared<tl::InputPeer>(input_peer(participant));
    const tl::messages_AffectedHistory result = call_rpc(req);
    const auto* h = std::get_if<ty::messages_affectedHistory>(&result.value);
    if (h == nullptr) throw std::runtime_error("unexpected messages.AffectedHistory variant");
    return AffectedHistoryInfo{h->pts, h->pts_count, h->offset};
}

std::vector<AdminLogEventInfo> Client::get_admin_log(const PeerRef& channel, const std::string& query,
                                                     const AdminLogFilterInfo& filter, std::int64_t max_id,
                                                     std::int64_t min_id, std::int32_t limit) {
    require_session();
    fn::channels_getAdminLog req;
    req.channel = input_channel_of(channel);
    req.q = query;
    req.max_id = max_id;
    req.min_id = min_id;
    req.limit = limit;
    if (filter.any_set()) {
        ty::channelAdminLogEventsFilter f;
        f.join = filter.join;
        f.leave = filter.leave;
        f.invite = filter.invite;
        f.ban = filter.ban;
        f.unban = filter.unban;
        f.kick = filter.kick;
        f.unkick = filter.unkick;
        f.promote = filter.promote;
        f.demote = filter.demote;
        f.info = filter.info;
        f.settings = filter.settings;
        f.pinned = filter.pinned;
        f.edit = filter.edit;
        f.delete_ = filter.del;
        f.group_call = filter.group_call;
        req.events_filter = std::make_shared<tl::ChannelAdminLogEventsFilter>(tl::ChannelAdminLogEventsFilter{f});
    }
    const tl::channels_AdminLogResults result = call_rpc(req);
    const auto* r = std::get_if<ty::channels_adminLogResults>(&result.value);
    if (r == nullptr) throw std::runtime_error("unexpected channels.AdminLogResults variant");
    cache_users_chats(r->users, r->chats);
    std::vector<AdminLogEventInfo> out;
    for (const auto& e : r->events) {
        if (!e) continue;
        const auto* ev = std::get_if<ty::channelAdminLogEvent>(&e->value);
        if (ev == nullptr) continue;
        AdminLogEventInfo info;
        info.id = ev->id;
        info.date = ev->date;
        info.user_id = ev->user_id;
        if (ev->action) {
            info.action = ev->action;
            info.action_constructor_id = std::visit(
                [](const auto& action) { return std::decay_t<decltype(action)>::kId; }, ev->action->value);
            if (const auto* edit = std::get_if<ty::channelAdminLogEventActionEditMessage>(&ev->action->value)) {
                if (edit->prev_message) info.previous_message = detail::message_from_tl(*edit->prev_message, cache_);
                if (edit->new_message) info.new_message = detail::message_from_tl(*edit->new_message, cache_);
            } else if (const auto* del = std::get_if<ty::channelAdminLogEventActionDeleteMessage>(&ev->action->value)) {
                if (del->message) info.deleted_message = detail::message_from_tl(*del->message, cache_);
            }
        }
        out.push_back(info);
    }
    return out;
}

std::vector<PeerRef> Client::get_admined_public_channels(bool by_location, bool check_limit, bool for_personal) {
    require_session();
    fn::channels_getAdminedPublicChannels req;
    if (by_location) req.by_location = true;
    if (check_limit) req.check_limit = true;
    if (for_personal) req.for_personal = true;
    return chats_as_peers(call_rpc(req));
}

std::vector<PeerRef> Client::get_channel_recommendations(const std::optional<PeerRef>& channel) {
    require_session();
    fn::channels_getChannelRecommendations req;
    if (channel) req.channel = input_channel_of(*channel);
    return chats_as_peers(call_rpc(req));
}

std::vector<PeerRef> Client::get_channels(const std::vector<PeerRef>& channels) {
    require_session();
    fn::channels_getChannels req;
    for (const auto& c : channels) req.id.push_back(input_channel_of(c));
    return chats_as_peers(call_rpc(req));
}

std::vector<PeerRef> Client::get_groups_for_discussion() {
    require_session();
    fn::channels_getGroupsForDiscussion req;
    return chats_as_peers(call_rpc(req));
}

std::vector<PeerRef> Client::get_send_as(const PeerRef& peer, bool for_paid_reactions, bool for_live_stories) {
    require_session();
    fn::channels_getSendAs req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    if (for_paid_reactions) req.for_paid_reactions = true;
    if (for_live_stories) req.for_live_stories = true;
    const tl::channels_SendAsPeers result = call_rpc(req);
    const auto* sp = std::get_if<ty::channels_sendAsPeers>(&result.value);
    if (sp == nullptr) throw std::runtime_error("unexpected channels.SendAsPeers variant");
    cache_users_chats(sp->users, sp->chats);
    std::vector<PeerRef> out;
    for (const auto& item : sp->peers) {
        if (!item) continue;
        if (const auto* s = std::get_if<ty::sendAsPeer>(&item->value)) {
            if (s->peer) out.push_back(detail::peer_from_tl(*s->peer, cache_));
        }
    }
    return out;
}

std::size_t Client::invite_to_channel(const PeerRef& channel, const std::vector<PeerRef>& users) {
    require_session();
    if (users.empty()) throw std::invalid_argument("invite_to_channel needs at least one user");
    fn::channels_inviteToChannel req;
    req.channel = input_channel_of(channel);
    for (const auto& u : users) req.users.push_back(input_user_of(u));
    const tl::messages_InvitedUsers result = call_rpc(req);
    const auto* inv = std::get_if<ty::messages_invitedUsers>(&result.value);
    if (inv == nullptr) throw std::runtime_error("unexpected messages.InvitedUsers variant");
    return inv->missing_invitees.size();
}

std::vector<Message> Client::search_posts(const std::string& hashtag, const std::string& query, std::int32_t offset_rate,
                                          const PeerRef& offset_peer, std::int32_t offset_id, std::int32_t limit,
                                          std::int64_t allow_paid_stars) {
    require_session();
    fn::channels_searchPosts req;
    if (!hashtag.empty()) req.hashtag = hashtag;
    if (!query.empty()) req.query = query;
    req.offset_rate = offset_rate;
    req.offset_peer = std::make_shared<tl::InputPeer>(input_peer(offset_peer));
    req.offset_id = offset_id;
    req.limit = limit;
    if (allow_paid_stars != 0) req.allow_paid_stars = allow_paid_stars;
    return collect_messages(call_rpc(req));
}

bool Client::set_channel_main_profile_tab(const PeerRef& channel, ProfileTab tab) {
    require_session();
    fn::channels_setMainProfileTab req;
    req.channel = input_channel_of(channel);
    switch (tab) {
        case ProfileTab::Posts: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabPosts{}}); break;
        case ProfileTab::Gifts: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabGifts{}}); break;
        case ProfileTab::Media: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabMedia{}}); break;
        case ProfileTab::Files: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabFiles{}}); break;
        case ProfileTab::Music: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabMusic{}}); break;
        case ProfileTab::Voice: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabVoice{}}); break;
        case ProfileTab::Links: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabLinks{}}); break;
        case ProfileTab::Gifs: req.tab = std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabGifs{}}); break;
    }
    return call_rpc(req);
}

bool Client::update_channel_emoji_status(const PeerRef& channel, const EmojiStatusInfo& status) {
    require_session();
    fn::channels_updateEmojiStatus req;
    req.channel = input_channel_of(channel);
    if (status.kind == EmojiStatusInfo::Kind::Empty) {
        req.emoji_status = std::make_shared<tl::EmojiStatus>(tl::EmojiStatus{ty::emojiStatusEmpty{}});
    } else {
        ty::emojiStatus st;
        st.document_id = status.document_id;
        if (status.until) st.until = *status.until;
        req.emoji_status = std::make_shared<tl::EmojiStatus>(tl::EmojiStatus{st});
    }
    (void)call_rpc(req);
    return true;
}

bool Client::update_paid_messages_price(const PeerRef& channel, std::int64_t stars, bool broadcast_messages_allowed) {
    require_session();
    fn::channels_updatePaidMessagesPrice req;
    req.channel = input_channel_of(channel);
    req.send_paid_messages_stars = stars;
    if (broadcast_messages_allowed) req.broadcast_messages_allowed = true;
    (void)call_rpc(req);
    return true;
}

}  // namespace cppgram
