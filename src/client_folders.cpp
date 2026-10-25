#include "cppgram/client.h"

#include <algorithm>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputUser> self_user() {
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}});
}

// Converts an InputPeer from a folder back to a PeerRef (keeps access hashes).
std::optional<PeerRef> peer_of_input(const tl::InputPeer& p) {
    if (const auto* u = std::get_if<ty::inputPeerUser>(&p.value)) {
        return PeerRef{PeerKind::User, u->user_id, u->access_hash, true};
    }
    if (const auto* c = std::get_if<ty::inputPeerChat>(&p.value)) {
        return PeerRef{PeerKind::Chat, c->chat_id, 0, false};
    }
    if (const auto* ch = std::get_if<ty::inputPeerChannel>(&p.value)) {
        return PeerRef{PeerKind::Channel, ch->channel_id, ch->access_hash, true};
    }
    return std::nullopt;
}

PeerRef peer_for_invite_user(std::int64_t id, std::int64_t access_hash, bool has_hash) {
    return PeerRef{PeerKind::User, id, access_hash, has_hash};
}

}  // namespace

std::vector<FolderInfo> Client::get_folders() {
    require_session();
    fn::messages_getDialogFilters req;
    const tl::messages_DialogFilters result = session_->call(req);
    std::vector<FolderInfo> out;
    const auto* list = std::get_if<ty::messages_dialogFilters>(&result.value);
    if (list == nullptr) return out;
    for (const auto& f : list->filters) {
        if (!f) continue;
        const auto* d = std::get_if<ty::dialogFilter>(&f->value);
        if (d == nullptr) continue;
        FolderInfo info;
        info.id = d->id;
        if (d->title) {
            if (const auto* t = std::get_if<ty::textWithEntities>(&d->title->value)) info.title = t->text;
        }
        info.contacts = d->contacts;
        info.non_contacts = d->non_contacts;
        info.groups = d->groups;
        info.broadcasts = d->broadcasts;
        info.bots = d->bots;
        info.exclude_muted = d->exclude_muted;
        info.exclude_read = d->exclude_read;
        info.exclude_archived = d->exclude_archived;
        for (const auto& p : d->include_peers) {
            if (!p) continue;
            if (auto ref = peer_of_input(*p)) info.include_peers.push_back(*ref);
        }
        for (const auto& p : d->exclude_peers) {
            if (!p) continue;
            if (auto ref = peer_of_input(*p)) info.exclude_peers.push_back(*ref);
        }
        out.push_back(std::move(info));
    }
    return out;
}


FolderInfo Client::create_folder(const FolderInfo& folder) {
    require_session();
    const auto existing = get_folders();
    std::int32_t next = 2;
    for (const auto& f : existing) next = std::max(next, f.id + 1);
    if (next > 255) throw std::runtime_error("no free folder id (limit is 255)");
    FolderInfo created = folder;
    created.id = next;
    edit_folder(created);
    return created;
}

void Client::edit_folder(const FolderInfo& folder) {
    require_session();
    if (folder.id < 2 || folder.id > 255) throw std::invalid_argument("folder id must be between 2 and 255");
    ty::dialogFilter d;
    d.id = folder.id;
    d.title = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{folder.title, {}}});
    d.contacts = folder.contacts;
    d.non_contacts = folder.non_contacts;
    d.groups = folder.groups;
    d.broadcasts = folder.broadcasts;
    d.bots = folder.bots;
    d.exclude_muted = folder.exclude_muted;
    d.exclude_read = folder.exclude_read;
    d.exclude_archived = folder.exclude_archived;
    for (const auto& p : folder.include_peers) {
        d.include_peers.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
    }
    for (const auto& p : folder.exclude_peers) {
        d.exclude_peers.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
    }
    fn::messages_updateDialogFilter req;
    req.id = folder.id;
    req.filter = std::make_shared<tl::DialogFilter>(tl::DialogFilter{d});
    session_->call(req);
}

void Client::delete_folder(std::int32_t folder_id) {
    require_session();
    fn::messages_updateDialogFilter req;
    req.id = folder_id;
    req.filter = std::nullopt;  // no filter means delete
    session_->call(req);
}

void Client::reorder_folders(const std::vector<std::int32_t>& folder_ids) {
    require_session();
    fn::messages_updateDialogFiltersOrder req;
    req.order = folder_ids;
    session_->call(req);
}

std::vector<std::string> Client::get_invite_links(const PeerRef& chat, std::int32_t limit) {
    require_session();
    fn::messages_getExportedChatInvites req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.admin_id = self_user();
    req.revoked = false;
    req.limit = limit;
    const tl::messages_ExportedChatInvites result = session_->call(req);
    std::vector<std::string> out;
    if (const auto* list = std::get_if<ty::messages_exportedChatInvites>(&result.value)) {
        for (const auto& inv : list->invites) {
            if (!inv) continue;
            if (const auto* e = std::get_if<ty::chatInviteExported>(&inv->value)) out.push_back(e->link);
        }
    }
    return out;
}

void Client::delete_invite_link(const PeerRef& chat, const std::string& link) {
    require_session();
    fn::messages_deleteExportedChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.link = link;
    session_->call(req);
}

std::vector<ChatInviteJoiner> Client::get_invite_joiners(const PeerRef& chat, const std::string& link, bool requested,
                                                         std::int32_t limit) {
    require_session();
    std::vector<ChatInviteJoiner> out;
    if (limit <= 0) return out;
    std::int32_t offset_date = 0;
    std::shared_ptr<tl::InputUser> offset_user = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserEmpty{}});
    while (static_cast<std::int32_t>(out.size()) < limit) {
        fn::messages_getChatInviteImporters req;
        req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
        if (!link.empty()) req.link = link;
        req.requested = requested;
        req.offset_date = offset_date;
        req.offset_user = offset_user;
        req.limit = std::min<std::int32_t>(100, limit - static_cast<std::int32_t>(out.size()));
        const tl::messages_ChatInviteImporters result = session_->call(req);
        const auto* importers = std::get_if<ty::messages_chatInviteImporters>(&result.value);
        if (importers == nullptr || importers->importers.empty()) break;
        cache_users_chats(importers->users, {});

        std::size_t added = 0;
        for (const auto& imp : importers->importers) {
            if (!imp) continue;
            const auto* ci = std::get_if<ty::chatInviteImporter>(&imp->value);
            if (ci == nullptr) continue;
            ChatInviteJoiner j;
            j.user = cache_.find(PeerKind::User, ci->user_id).value_or(peer_for_invite_user(ci->user_id, 0, false));
            j.date = ci->date;
            j.about = ci->about.value_or("");
            j.requested = ci->requested;
            // The next page starts after this importer.
            offset_date = ci->date;
            offset_user = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{
                ci->user_id, j.user.has_access_hash ? j.user.access_hash : 0}});
            out.push_back(std::move(j));
            ++added;
        }
        if (added == 0 || static_cast<std::int32_t>(importers->importers.size()) < req.limit) break;
    }
    return out;
}

void Client::approve_join_request(const PeerRef& chat, const PeerRef& user, bool approved) {
    require_session();
    fn::messages_hideChatJoinRequest req;
    req.approved = approved;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    if (user.kind != PeerKind::User) throw std::invalid_argument("approve_join_request expects a user");
    if (user.id == cache_.self_id()) {
        req.user_id = self_user();
    } else {
        PeerRef u = user;
        if (!u.has_access_hash) {
            if (const auto known = cache_.find(PeerKind::User, u.id)) u = *known;
        }
        if (!u.has_access_hash) throw std::runtime_error("user access hash unknown; resolve the user first");
        req.user_id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{u.id, u.access_hash}});
    }
    session_->call(req);
}

void Client::approve_all_join_requests(const PeerRef& chat, bool approved, const std::string& link) {
    require_session();
    fn::messages_hideAllChatJoinRequests req;
    req.approved = approved;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    if (!link.empty()) req.link = link;
    session_->call(req);
}

}  // namespace cppgram
