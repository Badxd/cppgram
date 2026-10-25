#include "cppgram/client.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t next_random_id() {
    static std::mt19937_64 engine{std::random_device{}()};
    return static_cast<std::int64_t>(engine());
}

std::shared_ptr<tl::InputChannel> input_channel_of(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a channel or supergroup peer");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{p.id, p.access_hash}});
}

}  // namespace

void Client::on_raw_update(RawUpdateHandler handler) {
    raw_update_handlers_.push_back(std::move(handler));
}

std::optional<Message> Client::copy_message(const PeerRef& from, const PeerRef& to, std::int32_t message_id) {
    require_session();
    fn::messages_forwardMessages req;
    req.drop_author = true;
    req.from_peer = std::make_shared<tl::InputPeer>(input_peer(from));
    req.to_peer = std::make_shared<tl::InputPeer>(input_peer(to));
    req.id = {message_id};
    req.random_id = {next_random_id()};
    const tl::Updates updates = session_->call(req);

    const std::vector<std::shared_ptr<tl::Update>>* list = nullptr;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        cache_users_chats(u->users, u->chats);
        list = &u->updates;
    } else if (const auto* c = std::get_if<ty::updatesCombined>(&updates.value)) {
        cache_users_chats(c->users, c->chats);
        list = &c->updates;
    }
    if (list == nullptr) return std::nullopt;
    for (const auto& up : *list) {
        if (!up) continue;
        if (const auto* nm = std::get_if<ty::updateNewMessage>(&up->value)) {
            if (nm->message) return detail::message_from_tl(*nm->message, cache_);
        } else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) {
            if (ncm->message) return detail::message_from_tl(*ncm->message, cache_);
        }
    }
    return std::nullopt;
}

std::vector<Message> Client::search_messages(const PeerRef& chat, const std::string& query, std::int32_t limit) {
    require_session();
    fn::messages_search req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.q = query;
    req.filter = std::make_shared<tl::MessagesFilter>(tl::MessagesFilter{ty::inputMessagesFilterEmpty{}});
    req.limit = limit;
    const tl::messages_Messages result = session_->call(req);
    std::vector<Message> out;
    std::visit(
        [&](const auto& v) {
            if constexpr (requires { v.users; v.chats; v.messages; }) {
                cache_users_chats(v.users, v.chats);
                for (const auto& m : v.messages) {
                    if (!m) continue;
                    if (auto msg = detail::message_from_tl(*m, cache_)) out.push_back(*msg);
                }
            }
        },
        result.value);
    return out;
}

void Client::send_reaction(const PeerRef& target, std::int32_t message_id, const std::string& emoji) {
    require_session();
    fn::messages_sendReaction req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.msg_id = message_id;
    if (!emoji.empty()) {
        req.reaction = std::vector<std::shared_ptr<tl::Reaction>>{
            std::make_shared<tl::Reaction>(tl::Reaction{ty::reactionEmoji{emoji}})};
    }
    session_->call(req);
}

void Client::mark_read(const PeerRef& target, std::int32_t max_id) {
    require_session();
    if (target.kind == PeerKind::Channel) {
        fn::channels_readHistory req;
        req.channel = input_channel_of(target);
        req.max_id = max_id;
        session_->call(req);
        return;
    }
    fn::messages_readHistory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.max_id = max_id;
    session_->call(req);
}

std::vector<Message> Client::get_dialogs_last_messages(std::int32_t limit) {
    require_session();
    fn::messages_getDialogs req;
    req.offset_peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerEmpty{}});
    req.limit = limit;
    const tl::messages_Dialogs result = session_->call(req);
    std::vector<Message> out;
    std::visit(
        [&](const auto& v) {
            if constexpr (requires { v.users; v.chats; v.messages; }) {
                cache_users_chats(v.users, v.chats);
                for (const auto& m : v.messages) {
                    if (!m) continue;
                    if (auto msg = detail::message_from_tl(*m, cache_)) out.push_back(*msg);
                }
            }
        },
        result.value);
    return out;
}

std::vector<User> Client::get_users(const std::vector<PeerRef>& users) {
    require_session();
    fn::users_getUsers req;
    for (PeerRef p : users) {
        if (p.kind != PeerKind::User) throw std::invalid_argument("get_users expects user peers");
        if (p.id == cache_.self_id()) {
            req.id.push_back(std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}}));
            continue;
        }
        if (!p.has_access_hash) {
            if (const auto known = cache_.find(PeerKind::User, p.id)) p = *known;
        }
        if (!p.has_access_hash) throw std::runtime_error("user access hash unknown; resolve the user first");
        req.id.push_back(std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{p.id, p.access_hash}}));
    }
    const auto raw = session_->call(req);
    cache_users_chats(raw, {});
    std::vector<User> out;
    for (const auto& u : raw) {
        if (!u) continue;
        if (auto user = detail::user_from_tl(*u)) out.push_back(*user);
    }
    return out;
}

void Client::create_group(const std::string& title, const std::vector<PeerRef>& users) {
    require_session();
    fn::messages_createChat req;
    req.title = title;
    for (const auto& p : users) {
        if (p.kind != PeerKind::User || !p.has_access_hash) {
            throw std::invalid_argument("create_group needs user peers with access hashes");
        }
        req.users.push_back(std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{p.id, p.access_hash}}));
    }
    session_->call(req);
}

PeerRef Client::create_channel(const std::string& title, const std::string& about, bool megagroup) {
    require_session();
    fn::channels_createChannel req;
    req.broadcast = !megagroup;
    req.megagroup = megagroup;
    req.title = title;
    req.about = about;
    const tl::Updates updates = session_->call(req);

    const std::vector<std::shared_ptr<tl::Chat>>* chats = nullptr;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        cache_users_chats(u->users, u->chats);
        chats = &u->chats;
    } else if (const auto* c = std::get_if<ty::updatesCombined>(&updates.value)) {
        cache_users_chats(c->users, c->chats);
        chats = &c->chats;
    }
    if (chats != nullptr) {
        for (const auto& chat : *chats) {
            if (!chat) continue;
            if (const auto* ch = std::get_if<ty::channel>(&chat->value)) {
                PeerRef out{PeerKind::Channel, ch->id, ch->access_hash.value_or(0), ch->access_hash.has_value()};
                return out;
            }
        }
    }
    throw std::runtime_error("channels.createChannel did not return the new channel");
}

void Client::ban_chat_member(const PeerRef& chat, const PeerRef& user, std::int32_t until_date) {
    require_session();
    if (chat.kind == PeerKind::Channel) {
        ty::chatBannedRights rights;
        rights.view_messages = true;
        rights.until_date = until_date;
        fn::channels_editBanned req;
        req.channel = input_channel_of(chat);
        req.participant = std::make_shared<tl::InputPeer>(input_peer(user));
        req.banned_rights = std::make_shared<tl::ChatBannedRights>(tl::ChatBannedRights{rights});
        session_->call(req);
        return;
    }
    if (chat.kind != PeerKind::Chat) throw std::invalid_argument("ban_chat_member expects a group or channel");
    if (user.kind != PeerKind::User || !user.has_access_hash) {
        throw std::invalid_argument("ban_chat_member needs a user peer with an access hash");
    }
    fn::messages_deleteChatUser req;
    req.chat_id = chat.id;
    req.revoke_history = true;
    req.user_id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
    session_->call(req);
}

void Client::unban_chat_member(const PeerRef& chat, const PeerRef& user) {
    require_session();
    fn::channels_editBanned req;
    req.channel = input_channel_of(chat);
    req.participant = std::make_shared<tl::InputPeer>(input_peer(user));
    req.banned_rights = std::make_shared<tl::ChatBannedRights>(tl::ChatBannedRights{ty::chatBannedRights{}});
    session_->call(req);
}

void Client::leave_group(const PeerRef& chat) {
    require_session();
    if (chat.kind != PeerKind::Chat) throw std::invalid_argument("leave_group expects a basic group");
    fn::messages_deleteChatUser req;
    req.chat_id = chat.id;
    req.revoke_history = false;
    req.user_id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}});
    session_->call(req);
}

}  // namespace cppgram
