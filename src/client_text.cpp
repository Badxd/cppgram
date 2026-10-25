#include "cppgram/client.h"

#include <random>
#include <stdexcept>

#include "cppgram/parser.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t random_id_text() {
    static std::mt19937_64 engine{std::random_device{}()};
    return static_cast<std::int64_t>(engine());
}

std::shared_ptr<tl::MessageEntity> to_tl_entity(const TextEntity& e) {
    switch (e.kind) {
        case TextEntity::Kind::Bold:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityBold{e.offset, e.length}});
        case TextEntity::Kind::Italic:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityItalic{e.offset, e.length}});
        case TextEntity::Kind::Underline:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityUnderline{e.offset, e.length}});
        case TextEntity::Kind::Strike:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityStrike{e.offset, e.length}});
        case TextEntity::Kind::Spoiler:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntitySpoiler{e.offset, e.length}});
        case TextEntity::Kind::Code:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityCode{e.offset, e.length}});
        case TextEntity::Kind::Pre:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityPre{e.offset, e.length, e.language}});
        case TextEntity::Kind::TextUrl:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityTextUrl{e.offset, e.length, e.url}});
        case TextEntity::Kind::MentionName:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityMentionName{e.offset, e.length, e.user_id}});
        case TextEntity::Kind::Blockquote:
            return std::make_shared<tl::MessageEntity>(tl::MessageEntity{ty::messageEntityBlockquote{false, e.offset, e.length}});
    }
    throw std::logic_error("unknown entity kind");
}

}  // namespace

Message Client::send_text(const PeerRef& target, const std::string& text, ParseMode mode,
                          std::optional<std::int32_t> reply_to_id) {
    require_session();
    FormattedText formatted = parse_text(text, mode);
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = formatted.text;
    req.random_id = random_id_text();
    if (reply_to_id) {
        req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{ty::inputReplyToMessage{*reply_to_id}});
    }
    if (!formatted.entities.empty()) {
        std::vector<std::shared_ptr<tl::MessageEntity>> entities;
        for (const auto& e : formatted.entities) entities.push_back(to_tl_entity(e));
        req.entities = std::move(entities);
    }
    const tl::Updates updates = session_->call(req);
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
        for (const auto& e : formatted.entities) entities.push_back(to_tl_entity(e));
        req.entities = std::move(entities);
    }
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, formatted.text, message_id)) return *msg;
    throw std::runtime_error("edit applied but the server returned no message");
}

}  // namespace cppgram
