#include "cppgram/client.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

std::vector<ConnectedBotInfo> Client::get_connected_bots() {
    require_session();
    fn::account_getConnectedBots req;
    const tl::account_ConnectedBots result = session_->call(req);
    const auto* list = std::get_if<ty::account_connectedBots>(&result.value);
    std::vector<ConnectedBotInfo> out;
    if (list == nullptr) return out;
    for (const auto& bot : list->connected_bots) {
        if (!bot) continue;
        const auto* cb = std::get_if<ty::connectedBot>(&bot->value);
        if (cb == nullptr) continue;
        ConnectedBotInfo info;
        info.bot_id = cb->bot_id;
        if (cb->date) info.date = *cb->date;
        if (cb->device) info.device = *cb->device;
        if (cb->location) info.location = *cb->location;
        out.push_back(std::move(info));
    }
    return out;
}

void Client::disconnect_connected_bot(const PeerRef& bot) {
    require_session();
    fn::account_updateConnectedBot req;
    req.deleted = true;
    req.bot = input_user_ptr(bot);
    req.recipients = std::make_shared<tl::InputBusinessBotRecipients>(
        tl::InputBusinessBotRecipients{ty::inputBusinessBotRecipients{}});
    session_->call(req);
}

}  // namespace cppgram
