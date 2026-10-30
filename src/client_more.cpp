#include "cppgram/client.h"
#include "secure_random.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

using mtproto::Bytes;
namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t next_random_id_more() {
    return detail::secure_random_i64();
}

std::shared_ptr<tl::InputChannel> channel_input(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a channel or supergroup");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{p.id, p.access_hash}});
}

}  // namespace

std::shared_ptr<tl::InputUser> Client::input_user_ptr(PeerRef user) const {
    if (user.kind != PeerKind::User) throw std::invalid_argument("expected a user peer");
    if (user.id == cache_.self_id()) {
        return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}});
    }
    if (!user.has_access_hash) {
        if (const auto known = cache_.find(PeerKind::User, user.id)) user = *known;
    }
    if (!user.has_access_hash) throw std::runtime_error("user access hash unknown; resolve the user first");
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
}

void Client::on_edited_message(MessageHandler handler) {
    edited_handlers_.push_back(std::move(handler));
}

void Client::on_inline_query(InlineQueryHandler handler) {
    inline_handlers_.push_back(std::move(handler));
}

// ---- messages (more)

std::vector<Message> Client::get_scheduled_messages(const PeerRef& chat) {
    require_session();
    fn::messages_getScheduledHistory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.hash = 0;
    return collect_messages(session_->call(req));
}

std::vector<Message> Client::get_discussion_replies(const PeerRef& chat, std::int32_t message_id, std::int32_t limit) {
    require_session();
    fn::messages_getReplies req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    req.limit = limit;
    return collect_messages(session_->call(req));
}

std::vector<Message> Client::search_global(const std::string& query, std::int32_t limit) {
    require_session();
    fn::messages_searchGlobal req;
    req.q = query;
    req.filter = std::make_shared<tl::MessagesFilter>(tl::MessagesFilter{ty::inputMessagesFilterEmpty{}});
    req.offset_peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerEmpty{}});
    req.limit = limit;
    return collect_messages(session_->call(req));
}

void Client::delete_chat_history(const PeerRef& chat, bool revoke) {
    require_session();
    if (chat.kind == PeerKind::Channel) {
        throw std::logic_error("delete_chat_history: channels are not wrapped yet");
    }
    fn::messages_deleteHistory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.max_id = 0;
    req.revoke = revoke;
    req.just_clear = false;
    session_->call(req);
}

void Client::answer_pre_checkout_query(std::int64_t query_id, bool success, const std::string& error) {
    require_session();
    fn::messages_setBotPrecheckoutResults req;
    req.query_id = query_id;
    req.success = success;
    if (!success && !error.empty()) req.error_ = error;
    session_->call(req);
}

std::string Client::request_callback_answer(const PeerRef& chat, std::int32_t message_id, const std::string& data) {
    require_session();
    fn::messages_getBotCallbackAnswer req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    req.data = Bytes(data.begin(), data.end());
    const tl::messages_BotCallbackAnswer answer = session_->call(req);
    if (const auto* a = std::get_if<ty::messages_botCallbackAnswer>(&answer.value)) {
        if (a->url) return *a->url;
        if (a->message) return *a->message;
    }
    return "";
}

// ---- invite links, titles, admins

std::string Client::export_chat_invite_link(const PeerRef& chat) {
    require_session();
    fn::messages_exportChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const tl::ExportedChatInvite invite = session_->call(req);
    if (const auto* e = std::get_if<ty::chatInviteExported>(&invite.value)) return e->link;
    throw std::runtime_error("export_chat_invite_link: join-request invite returned, use create_chat_invite_link");
}

std::string Client::create_chat_invite_link(const PeerRef& chat, std::optional<std::int32_t> expire_date,
                                            std::optional<std::int32_t> usage_limit, const std::string& title,
                                            bool request_needed) {
    require_session();
    fn::messages_exportChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.expire_date = expire_date;
    req.usage_limit = usage_limit;
    if (!title.empty()) req.title = title;
    req.request_needed = request_needed;
    const tl::ExportedChatInvite invite = session_->call(req);
    if (const auto* e = std::get_if<ty::chatInviteExported>(&invite.value)) return e->link;
    if (const auto* pj = std::get_if<ty::chatInvitePublicJoinRequests>(&invite.value)) {
        (void)pj;
        throw std::runtime_error("create_chat_invite_link: unexpected join-request invite type");
    }
    throw std::runtime_error("create_chat_invite_link: no link returned");
}

void Client::revoke_chat_invite_link(const PeerRef& chat, const std::string& link) {
    require_session();
    fn::messages_editExportedChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.link = link;
    req.revoked = true;
    session_->call(req);
}

void Client::set_chat_title(const PeerRef& chat, const std::string& title) {
    require_session();
    if (chat.kind == PeerKind::Channel) {
        fn::channels_editTitle req;
        req.channel = channel_input(chat);
        req.title = title;
        session_->call(req);
        return;
    }
    if (chat.kind != PeerKind::Chat) throw std::invalid_argument("set_chat_title expects a group or channel");
    fn::messages_editChatTitle req;
    req.chat_id = chat.id;
    req.title = title;
    session_->call(req);
}

void Client::promote_chat_member(const PeerRef& chat, const PeerRef& user, const AdminRights& rights,
                                 const std::string& rank) {
    require_session();
    ty::chatAdminRights r;
    r.change_info = rights.change_info;
    r.delete_messages = rights.delete_messages;
    r.ban_users = rights.ban_users;
    r.invite_users = rights.invite_users;
    r.pin_messages = rights.pin_messages;
    r.manage_call = rights.manage_call;
    r.manage_topics = rights.manage_topics;
    r.add_admins = rights.add_admins;
    r.anonymous = rights.anonymous;
    fn::channels_editAdmin req;
    req.channel = channel_input(chat);
    req.user_id = input_user_ptr(user);
    req.admin_rights = std::make_shared<tl::ChatAdminRights>(tl::ChatAdminRights{r});
    if (!rank.empty()) req.rank = rank;
    session_->call(req);
}

void Client::restrict_chat_member(const PeerRef& chat, const PeerRef& user, const Restrictions& restrictions,
                                  std::int32_t until_date) {
    require_session();
    ty::chatBannedRights rights;
    rights.send_messages = !restrictions.can_send_messages;
    rights.send_media = !restrictions.can_send_media;
    rights.send_stickers = !restrictions.can_send_stickers;
    rights.send_polls = !restrictions.can_send_polls;
    rights.embed_links = !restrictions.can_embed_links;
    rights.change_info = !restrictions.can_change_info;
    rights.invite_users = !restrictions.can_invite_users;
    rights.pin_messages = !restrictions.can_pin_messages;
    rights.until_date = until_date;
    fn::channels_editBanned req;
    req.channel = channel_input(chat);
    req.participant = std::make_shared<tl::InputPeer>(input_peer(user));
    req.banned_rights = std::make_shared<tl::ChatBannedRights>(tl::ChatBannedRights{rights});
    session_->call(req);
}

// ---- contacts, account

void Client::add_contact(const PeerRef& user, const std::string& first_name, const std::string& last_name,
                         const std::string& phone) {
    require_session();
    fn::contacts_addContact req;
    req.id = input_user_ptr(user);
    req.first_name = first_name;
    req.last_name = last_name;
    req.phone = phone;
    session_->call(req);
}

void Client::delete_contacts(const std::vector<PeerRef>& users) {
    require_session();
    fn::contacts_deleteContacts req;
    for (const auto& u : users) req.id.push_back(input_user_ptr(u));
    session_->call(req);
}

std::vector<User> Client::get_contacts() {
    require_session();
    fn::contacts_getContacts req;
    req.hash = 0;
    const tl::contacts_Contacts result = session_->call(req);
    std::vector<User> out;
    if (const auto* c = std::get_if<ty::contacts_contacts>(&result.value)) {
        cache_users_chats(c->users, {});
        for (const auto& u : c->users) {
            if (!u) continue;
            if (auto user = detail::user_from_tl(*u)) out.push_back(*user);
        }
    }
    return out;
}

std::vector<User> Client::import_contacts(const std::vector<ContactInput>& contacts) {
    require_session();
    fn::contacts_importContacts req;
    std::int64_t client_id = 1;
    for (const auto& c : contacts) {
        ty::inputPhoneContact pc;
        pc.client_id = client_id++;
        pc.phone = c.phone;
        pc.first_name = c.first_name;
        pc.last_name = c.last_name;
        req.contacts.push_back(std::make_shared<tl::InputContact>(tl::InputContact{pc}));
    }
    const tl::contacts_ImportedContacts result = session_->call(req);
    std::vector<User> out;
    std::visit(
        [&](const auto& v) {
            if constexpr (requires { v.users; }) {
                cache_users_chats(v.users, {});
                for (const auto& u : v.users) {
                    if (!u) continue;
                    if (auto user = detail::user_from_tl(*u)) out.push_back(*user);
                }
            }
        },
        result.value);
    return out;
}

void Client::block_user(const PeerRef& user) {
    require_session();
    fn::contacts_block req;
    req.id = std::make_shared<tl::InputPeer>(input_peer(user));
    session_->call(req);
}

void Client::unblock_user(const PeerRef& user) {
    require_session();
    fn::contacts_unblock req;
    req.id = std::make_shared<tl::InputPeer>(input_peer(user));
    session_->call(req);
}

User Client::update_profile(std::optional<std::string> first_name, std::optional<std::string> last_name,
                            std::optional<std::string> about) {
    require_session();
    fn::account_updateProfile req;
    req.first_name = std::move(first_name);
    req.last_name = std::move(last_name);
    req.about = std::move(about);
    const tl::User raw = session_->call(req);
    if (auto user = detail::user_from_tl(raw)) return *user;
    throw std::runtime_error("account.updateProfile returned an unusable user");
}

User Client::set_username(const std::string& username) {
    require_session();
    fn::account_updateUsername req;
    req.username = username;
    const tl::User raw = session_->call(req);
    if (auto user = detail::user_from_tl(raw)) return *user;
    throw std::runtime_error("account.updateUsername returned an unusable user");
}

void Client::log_out() {
    require_session();
    fn::auth_logOut req;
    session_->call(req);
    disconnect();
}

// ---- bots

void Client::set_bot_commands(const std::vector<BotCommandSpec>& commands, const std::string& lang_code) {
    require_session();
    fn::bots_setBotCommands req;
    req.scope = std::make_shared<tl::BotCommandScope>(tl::BotCommandScope{ty::botCommandScopeDefault{}});
    req.lang_code = lang_code;
    for (const auto& c : commands) {
        ty::botCommand bc;
        bc.command = c.command;
        bc.description = c.description;
        req.commands.push_back(std::make_shared<tl::BotCommand>(tl::BotCommand{bc}));
    }
    session_->call(req);
}

std::vector<BotCommandSpec> Client::get_bot_commands(const std::string& lang_code) {
    require_session();
    fn::bots_getBotCommands req;
    req.scope = std::make_shared<tl::BotCommandScope>(tl::BotCommandScope{ty::botCommandScopeDefault{}});
    req.lang_code = lang_code;
    const auto list = session_->call(req);
    std::vector<BotCommandSpec> out;
    for (const auto& c : list) {
        if (!c) continue;
        if (const auto* bc = std::get_if<ty::botCommand>(&c->value)) {
            out.push_back(BotCommandSpec{bc->command, bc->description});
        }
    }
    return out;
}

void Client::answer_inline_query(std::int64_t query_id, const std::vector<InlineArticle>& results,
                                 std::int32_t cache_time, bool personal) {
    require_session();
    fn::messages_setInlineBotResults req;
    req.query_id = query_id;
    req.cache_time = cache_time;
    req.private_ = personal;
    for (const auto& a : results) {
        ty::inputBotInlineMessageText text;
        text.message = a.text;
        ty::inputBotInlineResult r;
        r.id = a.id;
        r.type = "article";
        r.title = a.title;
        r.description = a.description;
        r.send_message = std::make_shared<tl::InputBotInlineMessage>(tl::InputBotInlineMessage{text});
        req.results.push_back(std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{r}));
    }
    session_->call(req);
}

}  // namespace cppgram
