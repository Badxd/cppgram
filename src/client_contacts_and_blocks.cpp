#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

// ---- phase 11: quick replies and blocked users

std::vector<QuickReplyInfo> Client::get_quick_replies() {
    require_session();
    fn::messages_getQuickReplies req;
    req.hash = 0;
    const tl::messages_QuickReplies res = session_->call(req);
    std::vector<QuickReplyInfo> out;
    const auto* list = std::get_if<ty::messages_quickReplies>(&res.value);
    if (list == nullptr) return out;
    for (const auto& q : list->quick_replies) {
        if (!q) continue;
        const auto* item = std::get_if<ty::quickReply>(&q->value);
        if (item == nullptr) continue;
        QuickReplyInfo info;
        info.shortcut_id = item->shortcut_id;
        info.shortcut = item->shortcut;
        info.message_count = item->count;
        out.push_back(info);
    }
    return out;
}

std::vector<BlockedUserInfo> Client::get_blocked_users(std::int32_t limit) {
    require_session();
    fn::contacts_getBlocked req;
    req.my_stories_from = false;
    req.offset = 0;
    req.limit = limit;
    const tl::contacts_Blocked res = session_->call(req);
    std::vector<BlockedUserInfo> out;
    const auto* list = std::get_if<ty::contacts_blocked>(&res.value);
    if (list == nullptr) return out;
    for (const auto& b : list->blocked) {
        if (!b) continue;
        const auto* pb = std::get_if<ty::peerBlocked>(&b->value);
        if (pb == nullptr || !pb->peer_id) continue;
        const auto* peer = std::get_if<ty::peerUser>(&pb->peer_id->value);
        if (peer == nullptr) continue;  // blocked chats are not listed here
        out.push_back(BlockedUserInfo{peer->user_id, pb->date});
    }
    return out;
}

// ---- phase 12: counts

std::int32_t Client::get_contacts_count() {
    require_session();
    fn::contacts_getContacts req;
    req.hash = 0;
    const tl::contacts_Contacts res = session_->call(req);
    const auto* c = std::get_if<ty::contacts_contacts>(&res.value);
    if (c == nullptr) return 0;
    return static_cast<std::int32_t>(c->contacts.size());
}

std::int32_t Client::get_dialogs_count() {
    require_session();
    fn::messages_getDialogs req;
    req.exclude_pinned = false;
    req.offset_date = 0;
    req.offset_id = 0;
    req.offset_peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerEmpty{}});
    req.limit = 1;
    req.hash = 0;
    const tl::messages_Dialogs res = session_->call(req);
    if (const auto* slice = std::get_if<ty::messages_dialogsSlice>(&res.value)) return slice->count;
    if (const auto* all = std::get_if<ty::messages_dialogs>(&res.value)) {
        return static_cast<std::int32_t>(all->dialogs.size());
    }
    return 0;
}

}  // namespace cppgram
