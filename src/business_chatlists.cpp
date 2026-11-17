// Business bot connection lookup and chatlist leave. Both return Updates; the effect arrives through the update stream.
#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

bool Client::get_bot_business_connection(const std::string& connection_id) {
    require_session();
    if (connection_id.empty()) throw std::invalid_argument("get_bot_business_connection needs a connection id");
    fn::account_getBotBusinessConnection req;
    req.connection_id = connection_id;
    (void)call_rpc(req);
    return true;
}

bool Client::leave_chatlist(std::int32_t folder_id, const std::vector<PeerRef>& peers) {
    require_session();
    fn::chatlists_leaveChatlist req;
    req.chatlist = std::make_shared<tl::InputChatlist>(tl::InputChatlist{ty::inputChatlistDialogFilter{folder_id}});
    for (const auto& p : peers) req.peers.push_back(std::make_shared<tl::InputPeer>(input_peer(p)));
    (void)call_rpc(req);
    return true;
}

}  // namespace cppgram
