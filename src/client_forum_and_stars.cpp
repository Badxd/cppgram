#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::optional<ForumTopicInfo> topic_info(const tl::ForumTopic& t) {
    const auto* f = std::get_if<ty::forumTopic>(&t.value);
    if (f == nullptr) return std::nullopt;
    ForumTopicInfo info;
    info.id = f->id;
    info.title = f->title;
    info.date = f->date;
    info.icon_color = f->icon_color;
    info.closed = f->closed;
    info.pinned = f->pinned;
    info.hidden = f->hidden;
    return info;
}

}  // namespace

// ---- phase 7: forum topics extras

void Client::set_forum_topic_closed(const PeerRef& channel, std::int32_t topic_id, bool closed) {
    require_session();
    fn::messages_editForumTopic req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(channel));
    req.topic_id = topic_id;
    req.closed = closed;
    session_->call(req);
}

void Client::set_general_topic_hidden(const PeerRef& channel, bool hidden) {
    require_session();
    fn::messages_editForumTopic req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(channel));
    req.topic_id = 1;
    req.hidden = hidden;
    session_->call(req);
}

std::vector<ForumTopicInfo> Client::get_forum_topics_by_id(const PeerRef& channel,
                                                           const std::vector<std::int32_t>& topic_ids) {
    require_session();
    fn::messages_getForumTopicsByID req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(channel));
    req.topics = topic_ids;
    const tl::messages_ForumTopics res = session_->call(req);
    std::vector<ForumTopicInfo> out;
    const auto* list = std::get_if<ty::messages_forumTopics>(&res.value);
    if (list == nullptr) return out;
    for (const auto& t : list->topics) {
        if (!t) continue;
        if (auto info = topic_info(*t)) out.push_back(*info);
    }
    return out;
}

// ---- phase 8: stars gifts and history

std::vector<StarGiftInfo> Client::get_star_gifts() {
    require_session();
    fn::payments_getStarGifts req;
    req.hash = 0;
    const tl::payments_StarGifts res = session_->call(req);
    std::vector<StarGiftInfo> out;
    const auto* list = std::get_if<ty::payments_starGifts>(&res.value);
    if (list == nullptr) return out;
    for (const auto& g : list->gifts) {
        if (!g) continue;
        const auto* gift = std::get_if<ty::starGift>(&g->value);
        if (gift == nullptr) continue;
        StarGiftInfo info;
        info.id = gift->id;
        info.stars = gift->stars;
        info.limited = gift->limited;
        info.sold_out = gift->sold_out;
        info.remaining = gift->availability_remains;
        info.total = gift->availability_total;
        out.push_back(info);
    }
    return out;
}

std::vector<StarsTransactionInfo> Client::get_stars_transactions(std::int32_t limit) {
    require_session();
    fn::payments_getStarsTransactions req;
    req.inbound = false;
    req.outbound = false;
    req.ascending = false;
    req.ton = false;
    req.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
    req.offset = "";
    req.limit = limit;
    const tl::payments_StarsStatus res = session_->call(req);
    std::vector<StarsTransactionInfo> out;
    const auto* status = std::get_if<ty::payments_starsStatus>(&res.value);
    if (status == nullptr || !status->history) return out;
    for (const auto& t : *status->history) {
        if (!t) continue;
        const auto* tx = std::get_if<ty::starsTransaction>(&t->value);
        if (tx == nullptr) continue;
        StarsTransactionInfo info;
        info.id = tx->id;
        info.date = tx->date;
        info.refund = tx->refund;
        info.pending = tx->pending;
        info.failed = tx->failed;
        if (tx->amount) {
            if (const auto* a = std::get_if<ty::starsAmount>(&tx->amount->value)) info.amount = a->amount;
        }
        out.push_back(info);
    }
    return out;
}

}  // namespace cppgram
