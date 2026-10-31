#include "cppgram/client.h"
#include "secure_random.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputPeer> channel_peer(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a supergroup with topics");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerChannel{p.id, p.access_hash}});
}

std::optional<ForumTopicInfo> topic_from_tl(const tl::ForumTopic& t) {
    const auto* ft = std::get_if<ty::forumTopic>(&t.value);
    if (ft == nullptr) return std::nullopt;
    ForumTopicInfo info;
    info.id = ft->id;
    info.title = ft->title;
    info.date = ft->date;
    info.icon_color = ft->icon_color;
    info.closed = ft->closed;
    info.pinned = ft->pinned;
    info.hidden = ft->hidden;
    return info;
}

std::int64_t random_id_forum() {
    return detail::secure_random_i64();
}

}  // namespace

ForumTopicInfo Client::create_forum_topic(const PeerRef& channel, const std::string& title, std::int32_t icon_color) {
    require_session();
    fn::messages_createForumTopic req;
    req.peer = channel_peer(channel);
    req.title = title;
    req.icon_color = icon_color;
    req.random_id = random_id_forum();
    const tl::Updates updates = session_->call(req);

    // The topic id is the id of the service message that created it.
    const std::vector<std::shared_ptr<tl::Update>>* list = nullptr;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        cache_users_chats(u->users, u->chats);
        list = &u->updates;
    } else if (const auto* c = std::get_if<ty::updatesCombined>(&updates.value)) {
        cache_users_chats(c->users, c->chats);
        list = &c->updates;
    }
    if (list != nullptr) {
        for (const auto& up : *list) {
            if (!up) continue;
            if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) {
                if (ncm->message) {
                    if (auto msg = detail::message_from_tl(*ncm->message, cache_)) {
                        ForumTopicInfo info;
                        info.id = msg->id;
                        info.title = title;
                        info.date = msg->date;
                        info.icon_color = icon_color;
                        return info;
                    }
                }
            }
        }
    }
    throw std::runtime_error("createForumTopic returned no service message");
}

std::vector<ForumTopicInfo> Client::get_forum_topics(const PeerRef& channel, std::int32_t limit, const std::string& query) {
    require_session();
    fn::messages_getForumTopics req;
    req.peer = channel_peer(channel);
    if (!query.empty()) req.q = query;
    req.offset_date = 0;
    req.offset_id = 0;
    req.offset_topic = 0;
    req.limit = limit;
    const tl::messages_ForumTopics result = session_->call(req);
    std::vector<ForumTopicInfo> out;
    if (const auto* ft = std::get_if<ty::messages_forumTopics>(&result.value)) {
        cache_users_chats(ft->users, ft->chats);
        for (const auto& t : ft->topics) {
            if (!t) continue;
            if (auto info = topic_from_tl(*t)) out.push_back(*info);
        }
    }
    return out;
}

void Client::edit_forum_topic(const PeerRef& channel, std::int32_t topic_id, std::optional<std::string> title,
                              std::optional<bool> closed, std::optional<bool> hidden) {
    require_session();
    fn::messages_editForumTopic req;
    req.peer = channel_peer(channel);
    req.topic_id = topic_id;
    req.title = std::move(title);
    req.closed = closed;
    req.hidden = hidden;
    session_->call(req);
}

void Client::pin_forum_topic(const PeerRef& channel, std::int32_t topic_id, bool pinned) {
    require_session();
    fn::messages_updatePinnedForumTopic req;
    req.peer = channel_peer(channel);
    req.topic_id = topic_id;
    req.pinned = pinned;
    session_->call(req);
}

void Client::delete_forum_topic_history(const PeerRef& channel, std::int32_t topic_id) {
    require_session();
    fn::messages_deleteTopicHistory req;
    req.peer = channel_peer(channel);
    req.top_msg_id = topic_id;
    session_->call(req);
}

std::vector<DialogInfo> Client::get_dialogs(std::int32_t limit, std::int32_t offset_date, std::int32_t offset_id,
                                            std::optional<PeerRef> offset_peer) {
    require_session();
    fn::messages_getDialogs req;
    req.offset_date = offset_date;
    req.offset_id = offset_id;
    req.offset_peer = std::make_shared<tl::InputPeer>(
        offset_peer ? input_peer(*offset_peer) : tl::InputPeer{ty::inputPeerEmpty{}});
    req.limit = limit;
    req.hash = 0;
    const tl::messages_Dialogs result = session_->call(req);

    std::vector<DialogInfo> out;
    std::visit(
        [&](const auto& v) {
            if constexpr (requires { v.dialogs; v.messages; v.users; v.chats; }) {
                cache_users_chats(v.users, v.chats);
                std::vector<Message> messages;
                for (const auto& m : v.messages) {
                    if (!m) continue;
                    if (auto msg = detail::message_from_tl(*m, cache_)) messages.push_back(*msg);
                }
                for (const auto& d : v.dialogs) {
                    if (!d) continue;
                    const auto* dd = std::get_if<ty::dialog>(&d->value);
                    if (dd == nullptr || !dd->peer) continue;
                    DialogInfo info;
                    info.peer = detail::peer_from_tl(*dd->peer, cache_);
                    info.top_message = dd->top_message;
                    info.unread_count = dd->unread_count;
                    info.pinned = dd->pinned;
                    for (const auto& msg : messages) {
                        if (msg.id == dd->top_message && msg.chat.kind == info.peer.kind && msg.chat.id == info.peer.id) {
                            info.top = msg;
                            break;
                        }
                    }
                    out.push_back(std::move(info));
                }
            }
        },
        result.value);
    return out;
}

std::vector<DialogInfo> Client::get_all_dialogs(std::size_t max_dialogs) {
    std::vector<DialogInfo> all;
    std::int32_t offset_date = 0;
    std::int32_t offset_id = 0;
    std::optional<PeerRef> offset_peer;
    const std::int32_t page = 100;
    while (all.size() < max_dialogs) {
        auto batch = get_dialogs(page, offset_date, offset_id, offset_peer);
        if (batch.empty()) break;
        const bool last_page = batch.size() < static_cast<std::size_t>(page);
        // Continue after the oldest top message of this page.
        const DialogInfo& last = batch.back();
        if (last.top) {
            offset_date = last.top->date;
            offset_id = last.top->id;
        } else {
            offset_date = 0;
            offset_id = last.top_message;
        }
        offset_peer = last.peer;
        for (auto& d : batch) {
            if (all.size() >= max_dialogs) break;
            all.push_back(std::move(d));
        }
        if (last_page) break;
    }
    return all;
}

}  // namespace cppgram
