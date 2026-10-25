#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

Message Client::reply(const Message& to, const std::string& text, ParseMode mode) {
    return send_text(to.chat, text, mode, to.id);
}

Message Client::edit(const Message& target, const std::string& text, ParseMode mode) {
    return edit_text(target.chat, target.id, text, mode);
}

void Client::remove(const Message& target) {
    delete_messages(target.chat, {target.id}, true);
}

void Client::pin(const Message& target, bool silent) {
    pin_message(target.chat, target.id, silent);
}

void Client::unpin(const Message& target) {
    unpin_message(target.chat, target.id);
}

void Client::react(const Message& target, const std::string& emoji) {
    send_reaction(target.chat, target.id, emoji);
}

std::vector<Message> Client::forward_to(const Message& source, const PeerRef& to) {
    return forward_messages(source.chat, to, {source.id});
}

std::vector<Message> Client::copy_to(const Message& source, const PeerRef& to) {
    return copy_messages(source.chat, to, {source.id}, false);
}

void Client::vote_poll(const Message& poll_message, const std::vector<std::size_t>& option_indexes) {
    require_session();
    if (!poll_message.poll) throw std::invalid_argument("vote_poll: message has no poll");
    fn::messages_sendVote req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(poll_message.chat));
    req.msg_id = poll_message.id;
    for (const auto idx : option_indexes) {
        if (idx >= poll_message.poll->options.size()) throw std::out_of_range("vote_poll: option index out of range");
        const std::string& id = poll_message.poll->options[idx].option_id;
        req.options.emplace_back(id.begin(), id.end());
    }
    session_->call(req);
}

}  // namespace cppgram
