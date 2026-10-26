#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

InviteLinkInfo to_invite_info(const tl::ExportedChatInvite& invite) {
    const auto* e = std::get_if<ty::chatInviteExported>(&invite.value);
    if (e == nullptr) {
        throw std::runtime_error("unexpected invite type (public join-request link)");
    }
    InviteLinkInfo out;
    out.link = e->link;
    out.title = e->title.value_or("");
    out.admin_id = e->admin_id;
    out.date = e->date;
    out.start_date = e->start_date;
    out.expire_date = e->expire_date;
    out.usage_limit = e->usage_limit;
    out.usage = e->usage.value_or(0);
    out.requested = e->requested.value_or(0);
    out.revoked = e->revoked;
    out.permanent = e->permanent;
    out.request_needed = e->request_needed;
    return out;
}

}  // namespace

InviteLinkInfo Client::get_invite_link(const PeerRef& chat, const std::string& link) {
    require_session();
    fn::messages_getExportedChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.link = link;
    const tl::messages_ExportedChatInvite res = session_->call(req);
    const auto* inv = std::get_if<ty::messages_exportedChatInvite>(&res.value);
    if (inv == nullptr || !inv->invite) {
        throw std::runtime_error("get_invite_link: no invite returned");
    }
    return to_invite_info(*inv->invite);
}

std::vector<InviteLinkInfo> Client::get_admin_invite_links(const PeerRef& chat, const PeerRef& admin, bool revoked,
                                                           std::int32_t limit) {
    require_session();
    fn::messages_getExportedChatInvites req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.admin_id = input_user_ptr(admin);
    req.revoked = revoked;
    req.limit = limit;
    const tl::messages_ExportedChatInvites res = session_->call(req);
    const auto* list = std::get_if<ty::messages_exportedChatInvites>(&res.value);
    std::vector<InviteLinkInfo> out;
    if (list == nullptr) return out;
    for (const auto& inv : list->invites) {
        if (inv) out.push_back(to_invite_info(*inv));
    }
    return out;
}

std::vector<AdminInviteCount> Client::get_admins_with_invites(const PeerRef& chat) {
    require_session();
    fn::messages_getAdminsWithInvites req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const tl::messages_ChatAdminsWithInvites res = session_->call(req);
    const auto* list = std::get_if<ty::messages_chatAdminsWithInvites>(&res.value);
    std::vector<AdminInviteCount> out;
    if (list == nullptr) return out;
    for (const auto& a : list->admins) {
        if (!a) continue;
        const auto* item = std::get_if<ty::chatAdminWithInvites>(&a->value);
        if (item == nullptr) continue;
        AdminInviteCount c;
        c.admin_id = item->admin_id;
        c.invites = item->invites_count;
        c.revoked_invites = item->revoked_invites_count;
        out.push_back(c);
    }
    return out;
}

InviteLinkInfo Client::edit_invite_link(const PeerRef& chat, const std::string& link,
                                        std::optional<std::int32_t> expire_date,
                                        std::optional<std::int32_t> usage_limit,
                                        std::optional<bool> request_needed,
                                        std::optional<std::string> title) {
    require_session();
    fn::messages_editExportedChatInvite req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.link = link;
    req.expire_date = expire_date;
    req.usage_limit = usage_limit;
    req.request_needed = request_needed;
    req.title = title;
    const tl::messages_ExportedChatInvite res = session_->call(req);
    if (const auto* inv = std::get_if<ty::messages_exportedChatInvite>(&res.value)) {
        if (!inv->invite) throw std::runtime_error("edit_invite_link: no invite returned");
        return to_invite_info(*inv->invite);
    }
    if (const auto* repl = std::get_if<ty::messages_exportedChatInviteReplaced>(&res.value)) {
        if (!repl->new_invite) throw std::runtime_error("edit_invite_link: no replacement invite returned");
        return to_invite_info(*repl->new_invite);
    }
    throw std::runtime_error("edit_invite_link: unexpected response");
}

void Client::delete_revoked_invite_links(const PeerRef& chat, const PeerRef& admin) {
    require_session();
    fn::messages_deleteRevokedExportedChatInvites req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.admin_id = input_user_ptr(admin);
    session_->call(req);
}

}  // namespace cppgram
