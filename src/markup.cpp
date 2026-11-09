#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "secure_random.h"

#include <random>
#include <stdexcept>

#include "cppgram/convert.h"
#include "markup_detail.h"

namespace cppgram {

using detail::to_tl_inline_markup;
using detail::to_tl_reply_markup;

using mtproto::Bytes;
namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t random_id_markup() {
    return detail::secure_random_i64();
}

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

[[noreturn]] void unsupported(const std::string& what) {
    throw errors::SerializationError("unsupported " + what + ": the public keyboard types cannot represent it");
}

bool has_reply_keyboard(const ReplyKeyboard& kb) {
    return !kb.rows.empty() || !kb.typed_rows.empty() || kb.force_reply || kb.hide;
}

// Empty when the style is unset, so the TL field is absent rather than present-but-null.
std::optional<std::shared_ptr<tl::KeyboardButtonStyle>> style_to_tl(const std::optional<ButtonStyle>& s) {
    if (!s) return std::nullopt;
    ty::keyboardButtonStyle out;
    out.bg_primary = s->primary;
    out.bg_danger = s->danger;
    out.bg_success = s->success;
    out.icon = s->icon;
    return std::make_shared<tl::KeyboardButtonStyle>(tl::KeyboardButtonStyle{out});
}

std::optional<ButtonStyle> style_from_tl(const std::optional<std::shared_ptr<tl::KeyboardButtonStyle>>& s) {
    if (!s || !*s) return std::nullopt;
    const auto* v = std::get_if<ty::keyboardButtonStyle>(&(*s)->value);
    if (v == nullptr) unsupported("keyboard button style");
    ButtonStyle out;
    out.primary = v->bg_primary;
    out.danger = v->bg_danger;
    out.success = v->bg_success;
    out.icon = v->icon;
    return out;
}

std::shared_ptr<tl::InlineQueryPeerType> peer_type_to_tl(InlineQueryPeer p) {
    switch (p) {
        case InlineQueryPeer::SameBotPm: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypeSameBotPM{}});
        case InlineQueryPeer::Pm: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypePM{}});
        case InlineQueryPeer::Chat: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypeChat{}});
        case InlineQueryPeer::Megagroup: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypeMegagroup{}});
        case InlineQueryPeer::Broadcast: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypeBroadcast{}});
        case InlineQueryPeer::BotPm: return std::make_shared<tl::InlineQueryPeerType>(tl::InlineQueryPeerType{ty::inlineQueryPeerTypeBotPM{}});
    }
    throw std::logic_error("unknown InlineQueryPeer");
}

std::optional<InlineQueryPeer> peer_type_from_tl(const tl::InlineQueryPeerType& p) {
    if (std::holds_alternative<ty::inlineQueryPeerTypeSameBotPM>(p.value)) return InlineQueryPeer::SameBotPm;
    if (std::holds_alternative<ty::inlineQueryPeerTypePM>(p.value)) return InlineQueryPeer::Pm;
    if (std::holds_alternative<ty::inlineQueryPeerTypeChat>(p.value)) return InlineQueryPeer::Chat;
    if (std::holds_alternative<ty::inlineQueryPeerTypeMegagroup>(p.value)) return InlineQueryPeer::Megagroup;
    if (std::holds_alternative<ty::inlineQueryPeerTypeBroadcast>(p.value)) return InlineQueryPeer::Broadcast;
    if (std::holds_alternative<ty::inlineQueryPeerTypeBotPM>(p.value)) return InlineQueryPeer::BotPm;
    return std::nullopt;
}

std::shared_ptr<tl::InputUser> input_user_of(std::int64_t id, std::int64_t access_hash) {
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{id, access_hash}});
}

}  // namespace

namespace detail {

std::shared_ptr<tl::KeyboardInlineButton> to_tl_inline_button(const InlineButton& b) {
    std::shared_ptr<tl::InlineButtonType> type;
    using K = InlineButton::Kind;
    switch (b.kind) {
        case K::Callback: {
            ty::inlineButtonTypeCallback cb;
            cb.requires_password = b.requires_password;
            cb.data = bytes_of(b.value);
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{cb});
            break;
        }
        case K::Url:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeUrl{b.value}});
            break;
        case K::SwitchInline: {
            ty::inlineButtonTypeSwitchInline sw;
            sw.same_peer = b.same_peer;
            sw.query = b.value;
            if (!b.peer_types.empty()) {
                std::vector<std::shared_ptr<tl::InlineQueryPeerType>> list;
                for (auto p : b.peer_types) list.push_back(peer_type_to_tl(p));
                sw.peer_types = std::move(list);
            }
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{sw});
            break;
        }
        case K::Copy:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeCopy{b.value}});
            break;
        case K::LoginUrl: {
            ty::inputInlineButtonTypeUrlAuth login;
            login.request_write_access = b.request_write_access;
            login.fwd_text = b.fwd_text;
            login.url = b.value;
            if (b.user_id != 0) login.bot = input_user_of(b.user_id, b.user_access_hash);
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{login});
            break;
        }
        case K::WebApp:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeWebView{b.value}});
            break;
        case K::Buy:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeBuy{}});
            break;
        case K::Game:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeGame{}});
            break;
        case K::UserProfile: {
            if (b.user_id == 0) throw std::invalid_argument("a user profile button needs a user id");
            ty::inputInlineButtonTypeUserProfile up;
            up.user_id = input_user_of(b.user_id, b.user_access_hash);
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{up});
            break;
        }
        case K::Disabled:
            type = std::make_shared<tl::InlineButtonType>(tl::InlineButtonType{ty::inlineButtonTypeDisabled{}});
            break;
    }
    ty::keyboardInlineButton out;
    out.style = style_to_tl(b.style);
    out.text = b.text;
    out.type = type;
    return std::make_shared<tl::KeyboardInlineButton>(tl::KeyboardInlineButton{out});
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

namespace {

std::shared_ptr<tl::KeyboardButton> reply_button_to_tl(const ReplyKeyboardButton& b) {
    std::shared_ptr<tl::ButtonType> type;
    using K = ReplyButtonKind;
    switch (b.kind) {
        case K::Text:
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{ty::buttonTypeDefault{}});
            break;
        case K::RequestPhone:
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{ty::buttonTypeRequestPhone{}});
            break;
        case K::RequestGeoLocation:
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{ty::buttonTypeRequestGeoLocation{}});
            break;
        case K::RequestPoll: {
            ty::buttonTypeRequestPoll poll;
            poll.quiz = b.poll_quiz;
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{poll});
            break;
        }
        case K::RequestUser:
        case K::RequestChat:
        case K::RequestChannel: {
            if (b.max_quantity < 1 || b.max_quantity > 10) {
                throw std::invalid_argument("a peer request button allows 1 to 10 peers");
            }
            ty::inputButtonTypeRequestPeer req;
            req.button_id = b.request_id;
            req.max_quantity = b.max_quantity;
            if (b.kind == K::RequestUser) {
                ty::requestPeerTypeUser u;
                u.bot = b.bot;
                u.premium = b.premium;
                req.peer_type = std::make_shared<tl::RequestPeerType>(tl::RequestPeerType{u});
            } else if (b.kind == K::RequestChat) {
                ty::requestPeerTypeChat c;
                c.creator = b.chat_creator;
                c.bot_participant = b.chat_bot_participant;
                c.has_username = b.has_username;
                c.forum = b.forum;
                req.peer_type = std::make_shared<tl::RequestPeerType>(tl::RequestPeerType{c});
            } else {
                ty::requestPeerTypeBroadcast ch;
                ch.creator = b.chat_creator;
                ch.has_username = b.has_username;
                req.peer_type = std::make_shared<tl::RequestPeerType>(tl::RequestPeerType{ch});
            }
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{req});
            break;
        }
        case K::WebApp: {
            ty::buttonTypeSimpleWebView web;
            web.url = b.url;
            type = std::make_shared<tl::ButtonType>(tl::ButtonType{web});
            break;
        }
    }
    ty::keyboardButton out;
    out.style = style_to_tl(b.style);
    out.text = b.text;
    out.type = type;
    return std::make_shared<tl::KeyboardButton>(tl::KeyboardButton{out});
}

}  // namespace

std::shared_ptr<tl::ReplyMarkup> to_tl_reply_markup(const ReplyKeyboard& kb) {
    const int forms = (kb.force_reply ? 1 : 0) + (kb.hide ? 1 : 0) + (!kb.rows.empty() ? 1 : 0) + (!kb.typed_rows.empty() ? 1 : 0);
    if (forms > 1) {
        throw std::invalid_argument("a reply keyboard takes one form: rows, typed_rows, force_reply or hide");
    }
    if (kb.force_reply) {
        ty::replyKeyboardForceReply fr;
        fr.single_use = kb.one_time;
        fr.selective = kb.selective;
        if (!kb.placeholder.empty()) fr.placeholder = kb.placeholder;
        return std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{fr});
    }
    if (kb.hide) {
        ty::replyKeyboardHide hide;
        hide.selective = kb.selective;
        return std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{hide});
    }
    ty::replyKeyboardMarkup markup;
    markup.resize = kb.resize;
    markup.single_use = kb.one_time;
    markup.selective = kb.selective;
    markup.persistent = kb.persistent;
    if (!kb.placeholder.empty()) markup.placeholder = kb.placeholder;
    for (const auto& row : kb.rows) {
        ty::keyboardButtonRow r;
        for (const auto& label : row) {
            ReplyKeyboardButton plain;
            plain.text = label;
            r.buttons.push_back(reply_button_to_tl(plain));
        }
        markup.rows.push_back(std::make_shared<tl::KeyboardButtonRow>(tl::KeyboardButtonRow{r}));
    }
    for (const auto& row : kb.typed_rows) {
        ty::keyboardButtonRow r;
        for (const auto& b : row) r.buttons.push_back(reply_button_to_tl(b));
        markup.rows.push_back(std::make_shared<tl::KeyboardButtonRow>(tl::KeyboardButtonRow{r}));
    }
    return std::make_shared<tl::ReplyMarkup>(tl::ReplyMarkup{markup});
}

InlineButton from_tl_inline_button(const tl::KeyboardInlineButton& b) {
    const auto* kb = std::get_if<ty::keyboardInlineButton>(&b.value);
    if (kb == nullptr || !kb->type) unsupported("inline keyboard button");
    InlineButton out;
    out.text = kb->text;
    out.style = style_from_tl(kb->style);
    const tl::InlineButtonType& t = *kb->type;
    if (const auto* cb = std::get_if<ty::inlineButtonTypeCallback>(&t.value)) {
        out.kind = InlineButton::Kind::Callback;
        out.value.assign(cb->data.begin(), cb->data.end());
        out.requires_password = cb->requires_password;
    } else if (const auto* url = std::get_if<ty::inlineButtonTypeUrl>(&t.value)) {
        out.kind = InlineButton::Kind::Url;
        out.value = url->url;
    } else if (const auto* login = std::get_if<ty::inlineButtonTypeUrlAuth>(&t.value)) {
        out.kind = InlineButton::Kind::LoginUrl;
        out.value = login->url;
        out.fwd_text = login->fwd_text;
        out.login_button_id = login->button_id;
    } else if (const auto* in_login = std::get_if<ty::inputInlineButtonTypeUrlAuth>(&t.value)) {
        out.kind = InlineButton::Kind::LoginUrl;
        out.value = in_login->url;
        out.fwd_text = in_login->fwd_text;
        out.request_write_access = in_login->request_write_access;
        if (in_login->bot && *in_login->bot) {
            if (const auto* u = std::get_if<ty::inputUser>(&(*in_login->bot)->value)) {
                out.user_id = u->user_id;
                out.user_access_hash = u->access_hash;
            }
        }
    } else if (const auto* web = std::get_if<ty::inlineButtonTypeWebView>(&t.value)) {
        out.kind = InlineButton::Kind::WebApp;
        out.value = web->url;
    } else if (std::holds_alternative<ty::inlineButtonTypeGame>(t.value)) {
        out.kind = InlineButton::Kind::Game;
    } else if (std::holds_alternative<ty::inlineButtonTypeBuy>(t.value)) {
        out.kind = InlineButton::Kind::Buy;
    } else if (const auto* sw = std::get_if<ty::inlineButtonTypeSwitchInline>(&t.value)) {
        out.kind = InlineButton::Kind::SwitchInline;
        out.value = sw->query;
        out.same_peer = sw->same_peer;
        if (sw->peer_types) {
            for (const auto& p : *sw->peer_types) {
                if (!p) continue;
                auto mapped = peer_type_from_tl(*p);
                if (!mapped) unsupported("inline query peer type");
                out.peer_types.push_back(*mapped);
            }
        }
    } else if (const auto* up = std::get_if<ty::inlineButtonTypeUserProfile>(&t.value)) {
        out.kind = InlineButton::Kind::UserProfile;
        out.user_id = up->user_id;
    } else if (const auto* in_up = std::get_if<ty::inputInlineButtonTypeUserProfile>(&t.value)) {
        out.kind = InlineButton::Kind::UserProfile;
        if (in_up->user_id) if (const auto* u = std::get_if<ty::inputUser>(&in_up->user_id->value)) {
            out.user_id = u->user_id;
            out.user_access_hash = u->access_hash;
        }
    } else if (const auto* cp = std::get_if<ty::inlineButtonTypeCopy>(&t.value)) {
        out.kind = InlineButton::Kind::Copy;
        out.value = cp->copy_text;
    } else if (std::holds_alternative<ty::inlineButtonTypeDisabled>(t.value)) {
        out.kind = InlineButton::Kind::Disabled;
    } else {
        unsupported("inline button type");
    }
    return out;
}

ReplyKeyboardButton from_tl_reply_button(const tl::KeyboardButton& b) {
    const auto* kb = std::get_if<ty::keyboardButton>(&b.value);
    if (kb == nullptr || !kb->type) unsupported("reply keyboard button");
    ReplyKeyboardButton out;
    out.text = kb->text;
    out.style = style_from_tl(kb->style);
    const tl::ButtonType& t = *kb->type;
    auto peer_fields = [&out](const tl::RequestPeerType& pt) {
        if (const auto* u = std::get_if<ty::requestPeerTypeUser>(&pt.value)) {
            out.kind = ReplyButtonKind::RequestUser;
            out.bot = u->bot;
            out.premium = u->premium;
            return;
        }
        if (const auto* c = std::get_if<ty::requestPeerTypeChat>(&pt.value)) {
            if (c->user_admin_rights || c->bot_admin_rights) unsupported("chat request with admin rights");
            out.kind = ReplyButtonKind::RequestChat;
            out.chat_creator = c->creator;
            out.chat_bot_participant = c->bot_participant;
            out.has_username = c->has_username;
            out.forum = c->forum;
            return;
        }
        if (const auto* ch = std::get_if<ty::requestPeerTypeBroadcast>(&pt.value)) {
            if (ch->user_admin_rights || ch->bot_admin_rights) unsupported("channel request with admin rights");
            out.kind = ReplyButtonKind::RequestChannel;
            out.chat_creator = ch->creator;
            out.has_username = ch->has_username;
            return;
        }
        unsupported("peer request type");
    };
    if (std::holds_alternative<ty::buttonTypeDefault>(t.value)) {
        out.kind = ReplyButtonKind::Text;
    } else if (std::holds_alternative<ty::buttonTypeRequestPhone>(t.value)) {
        out.kind = ReplyButtonKind::RequestPhone;
    } else if (std::holds_alternative<ty::buttonTypeRequestGeoLocation>(t.value)) {
        out.kind = ReplyButtonKind::RequestGeoLocation;
    } else if (const auto* poll = std::get_if<ty::buttonTypeRequestPoll>(&t.value)) {
        out.kind = ReplyButtonKind::RequestPoll;
        out.poll_quiz = poll->quiz;
    } else if (const auto* rp = std::get_if<ty::buttonTypeRequestPeer>(&t.value)) {
        if (!rp->peer_type) unsupported("peer request without a type");
        out.request_id = rp->button_id;
        out.max_quantity = rp->max_quantity;
        peer_fields(*rp->peer_type);
    } else if (const auto* irp = std::get_if<ty::inputButtonTypeRequestPeer>(&t.value)) {
        if (!irp->peer_type) unsupported("peer request without a type");
        if (irp->name_requested || irp->username_requested || irp->photo_requested) {
            unsupported("peer request with name, username or photo requested");
        }
        out.request_id = irp->button_id;
        out.max_quantity = irp->max_quantity;
        peer_fields(*irp->peer_type);
    } else if (const auto* web = std::get_if<ty::buttonTypeSimpleWebView>(&t.value)) {
        out.kind = ReplyButtonKind::WebApp;
        out.url = web->url;
    } else {
        unsupported("reply keyboard button type");
    }
    return out;
}

std::optional<InlineKeyboard> from_tl_inline_markup(const tl::ReplyMarkup& markup) {
    const auto* m = std::get_if<ty::replyInlineMarkup>(&markup.value);
    if (m == nullptr) return std::nullopt;
    InlineKeyboard out;
    for (const auto& row : m->rows) {
        if (!row) unsupported("empty inline keyboard row");
        const auto* rv = std::get_if<ty::keyboardInlineButtonRow>(&row->value);
        if (rv == nullptr) unsupported("inline keyboard row");
        std::vector<InlineButton> buttons;
        for (const auto& btn : rv->buttons) {
            if (!btn) unsupported("empty inline keyboard button");
            buttons.push_back(from_tl_inline_button(*btn));
        }
        out.push_back(std::move(buttons));
    }
    return out;
}

std::optional<ReplyKeyboard> from_tl_reply_markup(const tl::ReplyMarkup& markup) {
    if (const auto* hide = std::get_if<ty::replyKeyboardHide>(&markup.value)) {
        ReplyKeyboard out;
        out.hide = true;
        out.selective = hide->selective;
        return out;
    }
    if (const auto* fr = std::get_if<ty::replyKeyboardForceReply>(&markup.value)) {
        ReplyKeyboard out;
        out.force_reply = true;
        out.one_time = fr->single_use;
        out.selective = fr->selective;
        if (fr->placeholder) out.placeholder = *fr->placeholder;
        return out;
    }
    const auto* m = std::get_if<ty::replyKeyboardMarkup>(&markup.value);
    if (m == nullptr) return std::nullopt;
    ReplyKeyboard out;
    out.resize = m->resize;
    out.one_time = m->single_use;
    out.selective = m->selective;
    out.persistent = m->persistent;
    if (m->placeholder) out.placeholder = *m->placeholder;
    for (const auto& row : m->rows) {
        if (!row) unsupported("empty reply keyboard row");
        const auto* rv = std::get_if<ty::keyboardButtonRow>(&row->value);
        if (rv == nullptr) unsupported("reply keyboard row");
        std::vector<ReplyKeyboardButton> buttons;
        for (const auto& btn : rv->buttons) {
            if (!btn) unsupported("empty reply keyboard button");
            buttons.push_back(from_tl_reply_button(*btn));
        }
        out.typed_rows.push_back(std::move(buttons));
    }
    return out;
}

}  // namespace detail

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
    } else if (reply_keyboard && has_reply_keyboard(*reply_keyboard)) {
        req.reply_markup = to_tl_reply_markup(*reply_keyboard);
    }
    const tl::Updates updates = call_rpc(req);
    if (auto msg = find_sent_message(updates, target, text, 0)) return *msg;
    throw std::runtime_error("message was sent but no id was returned by the server");
}

void Client::edit_message_reply_markup(const PeerRef& target, std::int32_t message_id, const InlineKeyboard& keyboard) {
    require_session();
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.reply_markup = to_tl_inline_markup(keyboard);
    call_rpc(req);
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
    const tl::Updates updates = call_rpc(req);
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
