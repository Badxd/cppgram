#include "cppgram/client.h"

#include <memory>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;

namespace {

std::shared_ptr<mtproto::tl::InputDocument> input_document_of(const DocumentRef& d) {
    return std::make_shared<mtproto::tl::InputDocument>(
        mtproto::tl::InputDocument{ty::inputDocument{d.id, d.access_hash, d.file_reference}});
}

}  // namespace

void Client::archive_chats(const std::vector<PeerRef>& chats, bool archived) {
    require_session();
    for (const PeerRef& chat : chats) {
        archive_chat(chat, archived);
    }
}

bool Client::add_recent_sticker(const DocumentRef& sticker) {
    require_session();
    fn::messages_saveRecentSticker req;
    req.id = input_document_of(sticker);
    req.unsave = false;
    return session_->call(req);
}

bool Client::remove_recent_sticker(const DocumentRef& sticker) {
    require_session();
    fn::messages_saveRecentSticker req;
    req.id = input_document_of(sticker);
    req.unsave = true;
    return session_->call(req);
}

void Client::apply_gift_code(const std::string& slug) {
    require_session();
    if (slug.empty()) {
        throw std::invalid_argument("apply_gift_code: slug is empty");
    }
    fn::payments_applyGiftCode req;
    req.slug = slug;
    session_->call(req);
}

void Client::apply_boost(const PeerRef& chat, const std::vector<std::int32_t>& slots) {
    require_session();
    if (slots.empty()) {
        throw std::invalid_argument("apply_boost: no boost slots given");
    }
    fn::premium_applyBoost req;
    req.slots = slots;
    req.peer = std::make_shared<mtproto::tl::InputPeer>(input_peer(chat));
    session_->call(req);
}

}  // namespace cppgram
