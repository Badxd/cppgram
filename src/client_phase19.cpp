#include "cppgram/client.h"

#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

tl::InputSavedStarGift own_gift(std::int32_t msg_id) {
    return tl::InputSavedStarGift{ty::inputSavedStarGiftUser{msg_id}};
}

StarGiftCollectionInfo to_collection(const tl::StarGiftCollection& wrapper) {
    const auto* coll = std::get_if<ty::starGiftCollection>(&wrapper.value);
    if (coll == nullptr) throw std::runtime_error("star gift collection: unexpected result");
    StarGiftCollectionInfo out;
    out.id = coll->collection_id;
    out.title = coll->title;
    out.gifts_count = coll->gifts_count;
    return out;
}

std::vector<std::shared_ptr<tl::InputSavedStarGift>> own_gifts(const std::vector<std::int32_t>& ids) {
    std::vector<std::shared_ptr<tl::InputSavedStarGift>> out;
    out.reserve(ids.size());
    for (std::int32_t id : ids) out.push_back(std::make_shared<tl::InputSavedStarGift>(own_gift(id)));
    return out;
}

}  // namespace

StarGiftCollectionInfo Client::create_star_gift_collection(const PeerRef& peer, const std::string& title,
                                                           const std::vector<std::int32_t>& msg_ids) {
    require_session();
    fn::payments_createStarGiftCollection req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    req.title = title;
    req.stargift = own_gifts(msg_ids);
    const tl::StarGiftCollection result = session_->call(req);
    return to_collection(result);
}

StarGiftCollectionInfo Client::update_star_gift_collection(const PeerRef& peer, std::int32_t collection_id,
                                                           std::optional<std::string> title,
                                                           const std::vector<std::int32_t>& add_msg_ids,
                                                           const std::vector<std::int32_t>& remove_msg_ids) {
    require_session();
    fn::payments_updateStarGiftCollection req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    req.collection_id = collection_id;
    req.title = title;
    if (!add_msg_ids.empty()) req.add_stargift = own_gifts(add_msg_ids);
    if (!remove_msg_ids.empty()) req.delete_stargift = own_gifts(remove_msg_ids);
    const tl::StarGiftCollection result = session_->call(req);
    return to_collection(result);
}

}  // namespace cppgram
