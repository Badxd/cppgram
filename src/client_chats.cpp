#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputChannel> channel_of(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a channel or supergroup");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{p.id, p.access_hash}});
}

}  // namespace

void Client::mark_unread(const PeerRef& chat, bool unread) {
    require_session();
    fn::messages_markDialogUnread req;
    req.unread = unread;
    ty::inputDialogPeer dp;
    dp.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.peer = std::make_shared<tl::InputDialogPeer>(tl::InputDialogPeer{dp});
    session_->call(req);
}

void Client::archive_chat(const PeerRef& chat, bool archived) {
    require_session();
    fn::folders_editPeerFolders req;
    ty::inputFolderPeer fp;
    fp.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    fp.folder_id = archived ? 1 : 0;
    req.folder_peers.push_back(std::make_shared<tl::InputFolderPeer>(tl::InputFolderPeer{fp}));
    session_->call(req);
}

void Client::set_slow_mode(const PeerRef& channel, std::int32_t seconds) {
    require_session();
    if (seconds < 0) throw std::invalid_argument("slow mode seconds must be >= 0");
    fn::channels_toggleSlowMode req;
    req.channel = channel_of(channel);
    req.seconds = seconds;
    session_->call(req);
}

void Client::set_chat_permissions(const PeerRef& chat, const Restrictions& r) {
    require_session();
    ty::chatBannedRights rights;
    rights.send_messages = !r.can_send_messages;
    rights.send_media = !r.can_send_media;
    rights.send_stickers = !r.can_send_stickers;
    rights.send_polls = !r.can_send_polls;
    rights.embed_links = !r.can_embed_links;
    rights.change_info = !r.can_change_info;
    rights.invite_users = !r.can_invite_users;
    rights.pin_messages = !r.can_pin_messages;
    fn::messages_editChatDefaultBannedRights req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.banned_rights = std::make_shared<tl::ChatBannedRights>(tl::ChatBannedRights{rights});
    session_->call(req);
}

void Client::enable_forum(const PeerRef& channel, bool enabled) {
    require_session();
    fn::channels_toggleForum req;
    req.channel = channel_of(channel);
    req.enabled = enabled;
    req.tabs = true;
    session_->call(req);
}

}  // namespace cppgram
