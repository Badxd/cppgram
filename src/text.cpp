#include "cppgram/client.h"
#include "secure_random.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"
#include "cppgram/parser.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t random_id_text() {
    return detail::secure_random_i64();
}

}  // namespace

Message Client::send_text(const PeerRef& target, const std::string& text, ParseMode mode,
                          std::optional<std::int32_t> reply_to_id, std::optional<std::int64_t> effect_id) {
    require_session();
    FormattedText formatted = parse_text(text, mode);
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = formatted.text;
    req.random_id = random_id_text();
    if (reply_to_id) {
        req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{ty::inputReplyToMessage{*reply_to_id}});
    }
    req.effect = effect_id;
    if (!formatted.entities.empty()) {
        std::vector<std::shared_ptr<tl::MessageEntity>> entities;
        for (const auto& e : formatted.entities) entities.push_back(detail::to_tl_entity(e));
        req.entities = std::move(entities);
    }
    const tl::Updates updates = call_rpc(req);
    if (auto msg = find_sent_message(updates, target, formatted.text, 0)) return *msg;
    throw std::runtime_error("formatted message was sent but no id was returned by the server");
}

Message Client::edit_text(const PeerRef& target, std::int32_t message_id, const std::string& text, ParseMode mode) {
    require_session();
    FormattedText formatted = parse_text(text, mode);
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.message = formatted.text;
    if (!formatted.entities.empty()) {
        std::vector<std::shared_ptr<tl::MessageEntity>> entities;
        for (const auto& e : formatted.entities) entities.push_back(detail::to_tl_entity(e));
        req.entities = std::move(entities);
    }
    const tl::Updates updates = call_rpc(req);
    if (auto msg = find_sent_message(updates, target, formatted.text, message_id)) return *msg;
    throw std::runtime_error("edit applied but the server returned no message");
}

}  // namespace cppgram
