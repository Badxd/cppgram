#include "cppgram/client.h"

#include <memory>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

bool Client::edit_inline_message(std::int32_t dc_id, std::int64_t inline_id, std::int64_t access_hash,
                                 const std::string& text) {
    require_session();
    ty::inputBotInlineMessageID id;
    id.dc_id = dc_id;
    id.id = inline_id;
    id.access_hash = access_hash;
    fn::messages_editInlineBotMessage req;
    req.id = std::make_shared<tl::InputBotInlineMessageID>(tl::InputBotInlineMessageID{id});
    req.message = text;
    return session_->call(req);
}

}  // namespace cppgram
