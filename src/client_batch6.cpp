#include "cppgram/client.h"

#include <memory>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;

void Client::unpin_all_messages(const PeerRef& chat, std::optional<std::int32_t> top_msg_id) {
    require_session();
    fn::messages_unpinAllMessages req;
    req.peer = std::make_shared<mtproto::tl::InputPeer>(input_peer(chat));
    req.top_msg_id = top_msg_id;
    session_->call(req);
}

void Client::set_contact_note(const PeerRef& user, const std::string& note) {
    require_session();
    if (user.kind != PeerKind::User) {
        throw std::invalid_argument("set_contact_note: peer is not a user");
    }
    PeerRef p = user;
    if (!p.has_access_hash) {
        if (const auto known = cache_.find(PeerKind::User, p.id)) p = *known;
    }
    if (!p.has_access_hash) throw std::runtime_error("set_contact_note: user access hash unknown; resolve the user first");

    fn::contacts_updateContactNote req;
    req.id = std::make_shared<mtproto::tl::InputUser>(mtproto::tl::InputUser{ty::inputUser{p.id, p.access_hash}});
    req.note = std::make_shared<mtproto::tl::TextWithEntities>(
        mtproto::tl::TextWithEntities{ty::textWithEntities{note, {}}});
    session_->call(req);
}

bool Client::accept_terms_of_service(const std::string& terms_id) {
    require_session();
    if (terms_id.empty()) {
        throw std::invalid_argument("accept_terms_of_service: terms id is empty");
    }
    fn::help_acceptTermsOfService req;
    req.id = std::make_shared<mtproto::tl::DataJSON>(mtproto::tl::DataJSON{ty::dataJSON{terms_id}});
    return session_->call(req);
}

}  // namespace cppgram
