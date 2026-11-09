// wallet.* calls that do not need the 2FA password: proof challenge and user address lookup.
#include "cppgram/client.h"

#include <stdexcept>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

ProofChallengeInfo Client::get_wallet_proof_challenge() {
    require_session();
    fn::wallet_getProofChallenge req;
    const tl::wallet_ProofChallenge result = call_rpc(req);
    const auto* c = std::get_if<ty::wallet_proofChallenge>(&result.value);
    if (c == nullptr) throw std::runtime_error("unexpected wallet.ProofChallenge variant");
    return ProofChallengeInfo{c->payload, c->expires, c->domain};
}

std::vector<WalletUserAddressInfo> Client::get_wallet_user_addresses(const std::vector<PeerRef>& users,
                                                                     const std::vector<std::string>& addresses,
                                                                     bool force) {
    require_session();
    fn::wallet_getUserAddresses req;
    if (force) req.force = true;
    for (const auto& u : users) {
        if (u.kind != PeerKind::User || !u.has_access_hash) {
            throw std::invalid_argument("get_wallet_user_addresses needs user peers with access hashes");
        }
        req.id.push_back(std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{u.id, u.access_hash}}));
    }
    req.addresses = addresses;
    const tl::wallet_UserAddresses result = call_rpc(req);
    const auto* ua = std::get_if<ty::wallet_userAddresses>(&result.value);
    if (ua == nullptr) throw std::runtime_error("unexpected wallet.UserAddresses variant");
    std::vector<WalletUserAddressInfo> out;
    for (const auto& a : ua->addresses) {
        if (!a) continue;
        const auto* wa = std::get_if<ty::walletUserAddress>(&a->value);
        if (wa == nullptr) continue;
        WalletUserAddressInfo info;
        if (wa->user_id) {
            info.has_user = true;
            info.user_id = *wa->user_id;
        }
        info.address = wa->address;
        info.public_key = wa->public_key;
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace cppgram
