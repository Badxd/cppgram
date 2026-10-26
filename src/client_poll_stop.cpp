#include "cppgram/client.h"

#include <memory>
#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::TextWithEntities> plain_text(const std::string& text) {
    ty::textWithEntities t;
    t.text = text;
    return std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{t});
}

}  // namespace

void Client::stop_poll(const Message& poll_message) {
    require_session();
    if (!poll_message.poll) throw std::invalid_argument("stop_poll: message has no poll");
    const PollInfo& info = *poll_message.poll;

    // Editing the poll with the same question and answers and closed = true stops it on the server.
    ty::poll p;
    p.id = info.id;
    p.closed = true;
    p.public_voters = info.public_voters;
    p.multiple_choice = info.multiple_choice;
    p.quiz = info.quiz;
    p.question = plain_text(info.question);
    for (const auto& opt : info.options) {
        ty::pollAnswer answer;
        answer.text = plain_text(opt.text);
        answer.option.assign(opt.option_id.begin(), opt.option_id.end());
        p.answers.push_back(std::make_shared<tl::PollAnswer>(tl::PollAnswer{std::move(answer)}));
    }

    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(poll_message.chat));
    req.id = poll_message.id;
    req.media = std::make_shared<tl::InputMedia>(
        tl::InputMedia{ty::inputMediaPoll{std::make_shared<tl::Poll>(tl::Poll{std::move(p)})}});
    session_->call(req);
}

}  // namespace cppgram
