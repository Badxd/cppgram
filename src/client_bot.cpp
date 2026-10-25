#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputPhoto> input_photo_of(const MediaInfo& m) {
    if (m.kind != MediaKind::Photo) throw std::invalid_argument("expected a photo");
    return std::make_shared<tl::InputPhoto>(tl::InputPhoto{ty::inputPhoto{m.id, m.access_hash, m.file_reference}});
}

std::shared_ptr<tl::InputDocument> input_document_of(const MediaInfo& m) {
    if (m.kind != MediaKind::Document) throw std::invalid_argument("expected a document");
    return std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{m.id, m.access_hash, m.file_reference}});
}

std::shared_ptr<tl::InputBotInlineMessage> media_auto_message(const std::string& caption) {
    ty::inputBotInlineMessageMediaAuto msg;
    msg.message = caption;
    return std::make_shared<tl::InputBotInlineMessage>(tl::InputBotInlineMessage{msg});
}

}  // namespace

void Client::set_game_score(const PeerRef& chat, std::int32_t message_id, const PeerRef& user, std::int32_t score,
                            bool force, bool edit_message) {
    require_session();
    fn::messages_setGameScore req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = message_id;
    req.user_id = input_user_ptr(user);
    req.score = score;
    req.force = force;
    req.edit_message = edit_message;
    session_->call(req);
}

std::vector<GameScore> Client::get_game_high_scores(const PeerRef& chat, std::int32_t message_id, const PeerRef& user) {
    require_session();
    fn::messages_getGameHighScores req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = message_id;
    req.user_id = input_user_ptr(user);
    const tl::messages_HighScores result = session_->call(req);
    std::vector<GameScore> out;
    const auto* hs = std::get_if<ty::messages_highScores>(&result.value);
    if (hs == nullptr) return out;
    cache_users_chats(hs->users, {});
    for (const auto& s : hs->scores) {
        if (!s) continue;
        const auto* hsc = std::get_if<ty::highScore>(&s->value);
        if (hsc == nullptr) continue;
        GameScore g;
        g.user = cache_.find(PeerKind::User, hsc->user_id).value_or(PeerRef{PeerKind::User, hsc->user_id, 0, false});
        g.position = hsc->pos;
        g.score = hsc->score;
        out.push_back(g);
    }
    return out;
}

void Client::answer_inline_results(std::int64_t query_id, const std::vector<InlineResultSpec>& results,
                                   std::int32_t cache_time, bool personal) {
    require_session();
    fn::messages_setInlineBotResults req;
    req.query_id = query_id;
    req.cache_time = cache_time;
    req.private_ = personal;
    for (const auto& spec : results) {
        std::shared_ptr<tl::InputBotInlineResult> out;
        if (const auto* a = std::get_if<InlineArticle>(&spec)) {
            ty::inputBotInlineMessageText text;
            text.message = a->text;
            ty::inputBotInlineResult r;
            r.id = a->id;
            r.type = "article";
            r.title = a->title;
            r.description = a->description;
            r.send_message = std::make_shared<tl::InputBotInlineMessage>(tl::InputBotInlineMessage{text});
            out = std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{r});
        } else if (const auto* p = std::get_if<InlineCachedPhoto>(&spec)) {
            ty::inputBotInlineResultPhoto r;
            r.id = p->id;
            r.type = "photo";
            r.photo = input_photo_of(p->photo);
            r.send_message = media_auto_message(p->caption);
            out = std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{r});
        } else if (const auto* d = std::get_if<InlineCachedDocument>(&spec)) {
            ty::inputBotInlineResultDocument r;
            r.id = d->id;
            r.type = "document";
            r.title = d->title;
            r.description = d->description;
            r.document = input_document_of(d->document);
            r.send_message = media_auto_message(d->caption);
            out = std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{r});
        } else if (const auto* g = std::get_if<InlineGame>(&spec)) {
            ty::inputBotInlineResultGame r;
            r.id = g->id;
            r.short_name = g->short_name;
            ty::inputBotInlineMessageGame game;
            r.send_message = std::make_shared<tl::InputBotInlineMessage>(tl::InputBotInlineMessage{game});
            out = std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{r});
        }
        if (out) req.results.push_back(out);
    }
    session_->call(req);
}

}  // namespace cppgram
