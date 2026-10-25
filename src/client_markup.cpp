#include "cppgram/client.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

using mtproto::Bytes;
namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t random_id_markup() {
    static std::mt19937_64 engine{std::random_device{}()};
    return static_cast<std::int64_t>(engine());
}

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::shared_ptr<tl::KeyboardInlineButton> to_tl_inline_button(const InlineButton& b) {
    std::shared_ptr<tl::InlineButtonType> type;
    switch (b.kind) {
        case InlineButton::Kind::Callback:
            type = std::make_shared<tl::InlineButtonType>(
                tl::InlineButtonType{ty::inlineButtonTypeCallback{false, bytes_of(b.value)}});
            break;
        case InlineButton::Kind::Url:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeUrl{b.value}});
            break;
        case InlineButton::Kind::SwitchInline: {
            ty::inlineButtonTypeSwitchInline sw;
            sw.same_peer = b.same_peer;
            sw.query = b.value;
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{sw});
            break;
        }
        case InlineButton::Kind::Copy:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeCopy{b.value}});
            break;
    }
    return std::make_shared<tl::KeyboardInlineButton>(
        tl::KeyboardInlineButton{ty::keyboardInlineButton{std::nullopt, b.text, type}});
}

std::shared_ptr<tl::ReplyMarkup> to_tl_inline_markup(const InlineKeyboard& kb) {
    ty::replyInlineMarkup markup;
    for (const auto& row : kb) {
        ty::keyboardInlineButtonRow r;
        for (const auto& b : row) r.buttons.push_back(to_tl_inline_button(b));
        markup.rows.push_back(std::make_shared<tl::KeyboardInlineButtonRow>(tl::KeyboardInlineButtonRow{r}));
    }
    return std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{markup});
}

std::shared_ptr<tl::ReplyMarkup> to_tl_reply_markup(const ReplyKeyboard& kb) {
    ty::replyKeyboardMarkup markup;
    markup.resize = kb.resize;
    markup.single_use = kb.one_time;
    markup.selective = kb.selective;
    markup.persistent = kb.persistent;
    if (!kb.placeholder.empty()) markup.placeholder = kb.placeholder;
    for (const auto& row : kb.rows) {
        ty::keyboardButtonRow r;
        for (const auto& label : row) {
            ty::keyboardButton btn;
            btn.text = label;
            btn.type = std::make_shared<tl::ButtonType>(tl::ButtonType{ty::buttonTypeDefault{}});
            r.buttons.push_back(std::make_shared<tl::KeyboardButton>(tl::KeyboardButton{btn}));
        }
        markup.rows.push_back(std::make_shared<tl::KeyboardButtonRow>(tl::KeyboardButtonRow{r}));
    }
    return std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{markup});
}

}  // namespace

Message Client::send_with_markup(const PeerRef& target, const std::string& text,
                                 const std::optional<InlineKeyboard>& inline_keyboard,
                                 const std::optional<ReplyKeyboard>& reply_keyboard,
                                 std::optional<std::int32_t> reply_to_id) {
    require_session();
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = text;
    req.random_id = random_id_markup();
    if (reply_to_id) {
        req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{ty::inputReplyToMessage{*reply_to_id}});
    }
    if (inline_keyboard && !inline_keyboard->empty()) {
        req.reply_markup = to_tl_inline_markup(*inline_keyboard);
    } else if (reply_keyboard && !reply_keyboard->rows.empty()) {
        req.reply_markup = to_tl_reply_markup(*reply_keyboard);
    }
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, text, 0)) return *msg;
    throw std::runtime_error("message was sent but no id was returned by the server");
}

void Client::edit_message_reply_markup(const PeerRef& target, std::int32_t message_id, const InlineKeyboard& keyboard) {
    require_session();
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.reply_markup = to_tl_inline_markup(keyboard);
    session_->call(req);
}

Message Client::send_poll(const PeerRef& target, const std::string& question, const std::vector<std::string>& options,
                          bool anonymous, bool multiple, std::int32_t correct_option, const std::string& explanation) {
    require_session();
    if (options.size() < 2 || options.size() > 10) {
        throw std::invalid_argument("a poll needs between 2 and 10 options");
    }
    ty::poll p;
    p.id = random_id_markup();
    p.public_voters = !anonymous;
    p.multiple_choice = multiple;
    p.quiz = correct_option >= 0;
    p.question = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{question, {}}});
    for (std::size_t i = 0; i < options.size(); ++i) {
        ty::pollAnswer a;
        a.text = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{options[i], {}}});
        a.option = bytes_of(std::to_string(i));
        p.answers.push_back(std::make_shared<tl::PollAnswer>(tl::PollAnswer{a}));
    }
    ty::inputMediaPoll media;
    media.poll = std::make_shared<tl::Poll>(tl::Poll{p});
    if (correct_option >= 0) {
        if (static_cast<std::size_t>(correct_option) >= options.size()) {
            throw std::invalid_argument("correct_option is out of range");
        }
        media.correct_answers = std::vector<std::int32_t>{correct_option};
    }
    if (!explanation.empty()) media.solution = explanation;
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{media}), "");
}

std::vector<Message> Client::copy_messages(const PeerRef& from, const PeerRef& to,
                                           const std::vector<std::int32_t>& message_ids, bool drop_captions) {
    require_session();
    fn::messages_forwardMessages req;
    req.from_peer = std::make_shared<tl::InputPeer>(input_peer(from));
    req.to_peer = std::make_shared<tl::InputPeer>(input_peer(to));
    req.id = message_ids;
    req.drop_author = true;
    req.drop_media_captions = drop_captions;
    for (std::size_t i = 0; i < message_ids.size(); ++i) req.random_id.push_back(random_id_markup());
    const tl::Updates updates = session_->call(req);
    std::vector<Message> out;
    const std::vector<std::shared_ptr<tl::Update>>* list = nullptr;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        cache_users_chats(u->users, u->chats);
        list = &u->updates;
    } else if (const auto* c = std::get_if<ty::updatesCombined>(&updates.value)) {
        cache_users_chats(c->users, c->chats);
        list = &c->updates;
    }
    if (list == nullptr) return out;
    for (const auto& up : *list) {
        if (!up) continue;
        if (const auto* nm = std::get_if<ty::updateNewMessage>(&up->value)) {
            if (nm->message) if (auto m = detail::message_from_tl(*nm->message, cache_)) out.push_back(*m);
        } else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) {
            if (ncm->message) if (auto m = detail::message_from_tl(*ncm->message, cache_)) out.push_back(*m);
        }
    }
    return out;
}

Message Client::send_cached_media(const PeerRef& target, const MediaInfo& media, const std::string& caption) {
    require_session();
    std::shared_ptr<tl::InputMedia> input;
    if (media.kind == MediaKind::Document) {
        ty::inputMediaDocument doc;
        doc.id = std::make_shared<tl::InputDocument>(
            tl::InputDocument{ty::inputDocument{media.id, media.access_hash, media.file_reference}});
        input = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
    } else if (media.kind == MediaKind::Photo) {
        ty::inputMediaPhoto photo;
        photo.id = std::make_shared<tl::InputPhoto>(
            tl::InputPhoto{ty::inputPhoto{media.id, media.access_hash, media.file_reference}});
        input = std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
    } else {
        throw std::invalid_argument("send_cached_media: only photos and documents can be re-sent");
    }
    return send_media(target, input, caption);
}

Message Client::send_sticker(const PeerRef& target, const Message& source) {
    if (source.media.kind != MediaKind::Document) {
        throw std::invalid_argument("send_sticker: source message has no document");
    }
    return send_cached_media(target, source.media, "");
}

}  // namespace cppgram
