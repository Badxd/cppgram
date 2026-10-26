#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

// Accepts a full link (https://t.me/addlist/SLUG) or a bare slug.
std::string slug_of(const std::string& link) {
    const std::size_t slash = link.find_last_of('/');
    return slash == std::string::npos ? link : link.substr(slash + 1);
}

std::shared_ptr<tl::InputChatlist> chatlist_of(std::int32_t filter_id) {
    return std::make_shared<tl::InputChatlist>(tl::InputChatlist{ty::inputChatlistDialogFilter{filter_id}});
}

FolderInviteInfo to_folder_info(const tl::ExportedChatlistInvite& inv) {
    const auto* e = std::get_if<ty::exportedChatlistInvite>(&inv.value);
    if (e == nullptr) throw std::runtime_error("unexpected folder invite type");
    FolderInviteInfo out;
    out.url = e->url;
    out.title = e->title;
    out.peer_count = static_cast<std::int32_t>(e->peers.size());
    return out;
}

}  // namespace

FolderInviteInfo Client::create_folder_invite_link(std::int32_t filter_id, const std::string& title,
                                                   const std::vector<PeerRef>& peers) {
    require_session();
    fn::chatlists_exportChatlistInvite req;
    req.chatlist = chatlist_of(filter_id);
    req.title = title;
    for (const auto& p : peers) req.peers.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
    const tl::chatlists_ExportedChatlistInvite res = session_->call(req);
    const auto* w = std::get_if<ty::chatlists_exportedChatlistInvite>(&res.value);
    if (w == nullptr || !w->invite) throw std::runtime_error("create_folder_invite_link: no invite returned");
    return to_folder_info(*w->invite);
}

std::vector<FolderInviteInfo> Client::get_folder_invite_links(std::int32_t filter_id) {
    require_session();
    fn::chatlists_getExportedInvites req;
    req.chatlist = chatlist_of(filter_id);
    const tl::chatlists_ExportedInvites res = session_->call(req);
    std::vector<FolderInviteInfo> out;
    const auto* list = std::get_if<ty::chatlists_exportedInvites>(&res.value);
    if (list == nullptr) return out;
    for (const auto& inv : list->invites) {
        if (inv) out.push_back(to_folder_info(*inv));
    }
    return out;
}

FolderInviteInfo Client::edit_folder_invite_link(std::int32_t filter_id, const std::string& link,
                                                 std::optional<std::string> title,
                                                 std::optional<std::vector<PeerRef>> peers) {
    require_session();
    fn::chatlists_editExportedInvite req;
    req.chatlist = chatlist_of(filter_id);
    req.slug = slug_of(link);
    req.title = title;
    if (peers) {
        std::vector<std::shared_ptr<tl::InputPeer>> list;
        for (const auto& p : *peers) list.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
        req.peers = std::move(list);
    }
    const tl::ExportedChatlistInvite res = session_->call(req);
    return to_folder_info(res);
}

void Client::delete_folder_invite_link(std::int32_t filter_id, const std::string& link) {
    require_session();
    fn::chatlists_deleteExportedInvite req;
    req.chatlist = chatlist_of(filter_id);
    req.slug = slug_of(link);
    session_->call(req);
}

FolderInviteCheck Client::check_folder_invite_link(const std::string& link) {
    require_session();
    fn::chatlists_checkChatlistInvite req;
    req.slug = slug_of(link);
    const tl::chatlists_ChatlistInvite res = session_->call(req);
    FolderInviteCheck out;
    if (std::get_if<ty::chatlists_chatlistInviteAlready>(&res.value)) {
        out.already_joined = true;
        return out;
    }
    const auto* inv = std::get_if<ty::chatlists_chatlistInvite>(&res.value);
    if (inv == nullptr) throw std::runtime_error("check_folder_invite_link: unexpected response");
    if (inv->title) {
        if (const auto* t = std::get_if<ty::textWithEntities>(&inv->title->value)) out.title = t->text;
    }
    out.peer_count = static_cast<std::int32_t>(inv->peers.size());
    return out;
}

void Client::join_folder_invite_link(const std::string& link, const std::vector<PeerRef>& peers) {
    require_session();
    fn::chatlists_joinChatlistInvite req;
    req.slug = slug_of(link);
    for (const auto& p : peers) req.peers.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
    session_->call(req);
}

BoostsStatusInfo Client::get_boosts_status(const PeerRef& chat) {
    require_session();
    fn::premium_getBoostsStatus req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const tl::premium_BoostsStatus res = session_->call(req);
    const auto* s = std::get_if<ty::premium_boostsStatus>(&res.value);
    if (s == nullptr) throw std::runtime_error("get_boosts_status: unexpected response");
    BoostsStatusInfo out;
    out.my_boost = s->my_boost;
    out.level = s->level;
    out.current_level_boosts = s->current_level_boosts;
    out.boosts = s->boosts;
    out.gift_boosts = s->gift_boosts.value_or(0);
    out.next_level_boosts = s->next_level_boosts;
    out.boost_url = s->boost_url;
    return out;
}

std::int32_t Client::get_boosters_count(const PeerRef& chat) {
    require_session();
    fn::premium_getBoostsList req;
    req.gifts = false;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset = "";
    req.limit = 1;
    const tl::premium_BoostsList res = session_->call(req);
    const auto* l = std::get_if<ty::premium_boostsList>(&res.value);
    if (l == nullptr) return 0;
    return l->count;
}

std::int64_t Client::get_stars_balance() {
    require_session();
    fn::payments_getStarsStatus req;
    req.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
    const tl::payments_StarsStatus res = session_->call(req);
    const auto* s = std::get_if<ty::payments_starsStatus>(&res.value);
    if (s == nullptr || !s->balance) return 0;
    const auto* amount = std::get_if<ty::starsAmount>(&s->balance->value);
    return amount ? amount->amount : 0;
}

}  // namespace cppgram
