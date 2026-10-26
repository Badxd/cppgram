#include "cppgram/client.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

tl::InputSavedStarGift own_gift(std::int32_t msg_id) {
    return tl::InputSavedStarGift{ty::inputSavedStarGiftUser{msg_id}};
}

}  // namespace

bool Client::save_star_gift(std::int32_t msg_id, bool save) {
    require_session();
    fn::payments_saveStarGift req;
    req.unsave = !save;
    req.stargift = std::make_shared<tl::InputSavedStarGift>(own_gift(msg_id));
    return session_->call(req);
}

bool Client::convert_star_gift(std::int32_t msg_id) {
    require_session();
    fn::payments_convertStarGift req;
    req.stargift = std::make_shared<tl::InputSavedStarGift>(own_gift(msg_id));
    return session_->call(req);
}

void Client::upgrade_star_gift(std::int32_t msg_id, bool keep_original_details) {
    require_session();
    fn::payments_upgradeStarGift req;
    req.keep_original_details = keep_original_details;
    req.stargift = std::make_shared<tl::InputSavedStarGift>(own_gift(msg_id));
    session_->call(req);
}

void Client::transfer_star_gift(std::int32_t msg_id, const PeerRef& to) {
    require_session();
    fn::payments_transferStarGift req;
    req.stargift = std::make_shared<tl::InputSavedStarGift>(own_gift(msg_id));
    req.to_id = std::make_shared<tl::InputPeer>(input_peer(to));
    session_->call(req);
}

}  // namespace cppgram
