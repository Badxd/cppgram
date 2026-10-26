#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputChannel> channel_ref(const PeerRef& p) {
    ty::inputChannel c{};
    c.channel_id = p.id;
    c.access_hash = p.access_hash;
    return std::make_shared<tl::InputChannel>(tl::InputChannel{c});
}

}  // namespace

std::int32_t Client::get_chat_online_count(const PeerRef& chat) {
    require_session();
    fn::messages_getOnlines req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const tl::ChatOnlines res = session_->call(req);
    const auto* o = std::get_if<ty::chatOnlines>(&res.value);
    return o ? o->onlines : 0;
}

std::int32_t Client::get_chat_history_count(const PeerRef& chat) {
    require_session();
    fn::messages_getHistory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset_id = 0;
    req.offset_date = 0;
    req.add_offset = 0;
    req.limit = 1;
    req.max_id = 0;
    req.min_id = 0;
    req.hash = 0;
    const tl::messages_Messages res = session_->call(req);
    if (const auto* slice = std::get_if<ty::messages_messagesSlice>(&res.value)) return slice->count;
    if (const auto* all = std::get_if<ty::messages_messages>(&res.value)) {
        return static_cast<std::int32_t>(all->messages.size());
    }
    if (const auto* ch = std::get_if<ty::messages_channelMessages>(&res.value)) return ch->count;
    return 0;
}

void Client::delete_chat_photo(const PeerRef& chat) {
    require_session();
    ty::inputChatPhotoEmpty empty{};
    if (chat.kind == PeerKind::Channel) {
        fn::channels_editPhoto req;
        req.channel = channel_ref(chat);
        req.photo = std::make_shared<tl::InputChatPhoto>(tl::InputChatPhoto{empty});
        session_->call(req);
        return;
    }
    if (chat.kind != PeerKind::Chat) throw std::invalid_argument("delete_chat_photo expects a group or channel");
    fn::messages_editChatPhoto req;
    req.chat_id = chat.id;
    req.photo = std::make_shared<tl::InputChatPhoto>(tl::InputChatPhoto{empty});
    session_->call(req);
}

}  // namespace cppgram
