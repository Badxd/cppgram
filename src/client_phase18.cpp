#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::optional<SavedStarGiftInfo> to_saved_gift(const tl::SavedStarGift& wrapper) {
    const auto* found = std::get_if<ty::savedStarGift>(&wrapper.value);
    if (found == nullptr) return std::nullopt;
    const auto& g = *found;
    SavedStarGiftInfo out;
    if (g.msg_id) out.msg_id = *g.msg_id;
    if (g.saved_id) out.saved_id = *g.saved_id;
    if (g.gift) {
        if (const auto* regular = std::get_if<ty::starGift>(&g.gift->value)) {
            out.gift_id = regular->id;
        } else if (const auto* unique = std::get_if<ty::starGiftUnique>(&g.gift->value)) {
            out.unique_slug = unique->slug;
        }
    }
    out.date = g.date;
    out.unsaved = g.unsaved;
    out.name_hidden = g.name_hidden;
    out.pinned_to_top = g.pinned_to_top;
    out.can_upgrade = g.can_upgrade;
    out.convert_stars = g.convert_stars;
    out.upgrade_stars = g.upgrade_stars;
    out.transfer_stars = g.transfer_stars;
    if (g.message) {
        if (const auto* text = std::get_if<ty::textWithEntities>(&(*g.message)->value)) out.message = text->text;
    }
    return out;
}

}  // namespace

SavedStarGiftPage Client::get_saved_star_gifts(const PeerRef& peer, const std::string& offset, std::int32_t limit,
                                               std::optional<std::int32_t> collection_id) {
    require_session();
    fn::payments_getSavedStarGifts req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    req.offset = offset;
    req.limit = limit;
    req.collection_id = collection_id;
    const tl::payments_SavedStarGifts result = session_->call(req);
    const auto* page = std::get_if<ty::payments_savedStarGifts>(&result.value);
    if (page == nullptr) throw std::runtime_error("get_saved_star_gifts: unexpected result");

    SavedStarGiftPage out;
    out.count = page->count;
    if (page->next_offset) out.next_offset = *page->next_offset;
    for (const auto& gift : page->gifts) {
        if (!gift) continue;
        if (auto info = to_saved_gift(*gift)) out.gifts.push_back(std::move(*info));
    }
    return out;
}

std::vector<StarGiftCollectionInfo> Client::get_star_gift_collections(const PeerRef& peer) {
    require_session();
    fn::payments_getStarGiftCollections req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    req.hash = 0;
    const tl::payments_StarGiftCollections result = session_->call(req);
    const auto* list = std::get_if<ty::payments_starGiftCollections>(&result.value);
    if (list == nullptr) throw std::runtime_error("get_star_gift_collections: unexpected result");

    std::vector<StarGiftCollectionInfo> out;
    for (const auto& c : list->collections) {
        if (!c) continue;
        const auto* coll = std::get_if<ty::starGiftCollection>(&c->value);
        if (coll == nullptr) continue;
        StarGiftCollectionInfo info;
        info.id = coll->collection_id;
        info.title = coll->title;
        info.gifts_count = coll->gifts_count;
        out.push_back(std::move(info));
    }
    return out;
}

UniqueStarGiftInfo Client::get_unique_star_gift(const std::string& slug) {
    require_session();
    fn::payments_getUniqueStarGift req;
    req.slug = slug;
    const tl::payments_UniqueStarGift result = session_->call(req);
    const auto* wrapped = std::get_if<ty::payments_uniqueStarGift>(&result.value);
    if (wrapped == nullptr || !wrapped->gift) throw std::runtime_error("get_unique_star_gift: unexpected result");
    const auto* unique = std::get_if<ty::starGiftUnique>(&wrapped->gift->value);
    if (unique == nullptr) throw std::runtime_error("get_unique_star_gift: gift is not unique");

    UniqueStarGiftInfo out;
    out.id = unique->id;
    out.gift_id = unique->gift_id;
    out.title = unique->title;
    out.slug = unique->slug;
    out.num = unique->num;
    return out;
}

std::vector<std::int64_t> Client::get_star_gift_upgrade_prices(std::int64_t gift_id) {
    require_session();
    fn::payments_getStarGiftUpgradePreview req;
    req.gift_id = gift_id;
    const tl::payments_StarGiftUpgradePreview result = session_->call(req);
    const auto* preview = std::get_if<ty::payments_starGiftUpgradePreview>(&result.value);
    if (preview == nullptr) throw std::runtime_error("get_star_gift_upgrade_prices: unexpected result");

    std::vector<std::int64_t> out;
    for (const auto& price : preview->prices) {
        if (!price) continue;
        if (const auto* p = std::get_if<ty::starGiftUpgradePrice>(&price->value)) out.push_back(p->upgrade_stars);
    }
    return out;
}

}  // namespace cppgram
