// Read-only Premium, app config, contacts and saved music calls. Each call maps the TL reply to public types.
#include "cppgram/client.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

DocumentRef document_ref_from(const tl::Document& d) {
    DocumentRef out;
    if (const auto* doc = std::get_if<ty::document>(&d.value)) {
        out.id = doc->id;
        out.access_hash = doc->access_hash;
        out.file_reference = doc->file_reference;
    }
    return out;
}

// Serializes a JSONValue tree. Unknown alternatives become null rather than failing the whole config.
nlohmann::json json_from_tl(const tl::JSONValue& value) {
    if (std::holds_alternative<ty::jsonNull>(value.value)) return nullptr;
    if (const auto* b = std::get_if<ty::jsonBool>(&value.value)) return b->value;
    if (const auto* n = std::get_if<ty::jsonNumber>(&value.value)) return n->value;
    if (const auto* s = std::get_if<ty::jsonString>(&value.value)) return s->value;
    if (const auto* a = std::get_if<ty::jsonArray>(&value.value)) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : a->value) {
            arr.push_back(item ? json_from_tl(*item) : nlohmann::json(nullptr));
        }
        return arr;
    }
    if (const auto* o = std::get_if<ty::jsonObject>(&value.value)) {
        nlohmann::json obj = nlohmann::json::object();
        for (const auto& entry : o->value) {
            if (!entry) continue;
            const auto* kv = std::get_if<ty::jsonObjectValue>(&entry->value);
            if (kv == nullptr) continue;
            obj[kv->key] = kv->value ? json_from_tl(*kv->value) : nlohmann::json(nullptr);
        }
        return obj;
    }
    return nullptr;
}

}  // namespace

std::vector<MyBoostInfo> Client::get_my_boosts() {
    require_session();
    fn::premium_getMyBoosts req;
    const tl::premium_MyBoosts result = call_rpc(req);
    const auto* boosts = std::get_if<ty::premium_myBoosts>(&result.value);
    if (boosts == nullptr) throw std::runtime_error("unexpected premium.MyBoosts variant");
    cache_users_chats(boosts->users, boosts->chats);

    std::vector<MyBoostInfo> out;
    for (const auto& item : boosts->my_boosts) {
        if (!item) continue;
        const auto* b = std::get_if<ty::myBoost>(&item->value);
        if (b == nullptr) continue;
        MyBoostInfo info;
        info.slot = b->slot;
        if (b->peer && *b->peer) {
            info.has_peer = true;
            info.peer = detail::peer_from_tl(**b->peer, cache_);
        }
        info.date = b->date;
        info.expires = b->expires;
        if (b->cooldown_until_date) info.cooldown_until_date = *b->cooldown_until_date;
        out.push_back(std::move(info));
    }
    return out;
}

AppConfigInfo Client::get_app_config(std::int32_t hash) {
    require_session();
    fn::help_getAppConfig req;
    req.hash = hash;
    const tl::help_AppConfig result = call_rpc(req);
    const auto* cfg = std::get_if<ty::help_appConfig>(&result.value);
    if (cfg == nullptr) throw std::runtime_error("unexpected help.AppConfig variant");
    AppConfigInfo out;
    out.hash = cfg->hash;
    if (!cfg->config) throw std::runtime_error("help.appConfig carried no config");
    out.json = json_from_tl(*cfg->config).dump();
    return out;
}

PeerRef Client::resolve_phone(const std::string& phone) {
    require_session();
    if (phone.empty()) throw std::invalid_argument("resolve_phone needs a phone number");
    fn::contacts_resolvePhone req;
    req.phone = phone;
    const tl::contacts_ResolvedPeer result = call_rpc(req);
    const auto* resolved = std::get_if<ty::contacts_resolvedPeer>(&result.value);
    if (resolved == nullptr) throw std::runtime_error("unexpected contacts.ResolvedPeer variant");
    cache_users_chats(resolved->users, resolved->chats);
    if (!resolved->peer) throw std::runtime_error("contacts.resolvePhone returned no peer");
    return detail::peer_from_tl(*resolved->peer, cache_);
}

ContactSearchResult Client::search_contacts(const std::string& query, std::int32_t limit, bool broadcasts,
                                            bool bots) {
    require_session();
    fn::contacts_search req;
    req.q = query;
    req.limit = limit;
    req.broadcasts = broadcasts;
    req.bots = bots;
    const tl::contacts_Found result = call_rpc(req);
    const auto* found = std::get_if<ty::contacts_found>(&result.value);
    if (found == nullptr) throw std::runtime_error("unexpected contacts.Found variant");
    cache_users_chats(found->users, found->chats);

    ContactSearchResult out;
    for (const auto& p : found->my_results) {
        if (p) out.my_results.push_back(detail::peer_from_tl(*p, cache_));
    }
    for (const auto& p : found->results) {
        if (p) out.results.push_back(detail::peer_from_tl(*p, cache_));
    }
    return out;
}

SavedMusicPage Client::get_saved_music(const PeerRef& user, std::int32_t offset, std::int32_t limit,
                                       std::int64_t hash) {
    require_session();
    if (user.kind != PeerKind::User || !user.has_access_hash) {
        throw std::invalid_argument("get_saved_music needs a user peer with an access hash");
    }
    fn::users_getSavedMusic req;
    req.id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
    req.offset = offset;
    req.limit = limit;
    req.hash = hash;
    const tl::users_SavedMusic result = call_rpc(req);

    SavedMusicPage out;
    if (std::holds_alternative<ty::users_savedMusicNotModified>(result.value)) {
        out.not_modified = true;
        out.count = std::get<ty::users_savedMusicNotModified>(result.value).count;
        return out;
    }
    const auto* music = std::get_if<ty::users_savedMusic>(&result.value);
    if (music == nullptr) throw std::runtime_error("unexpected users.SavedMusic variant");
    out.count = music->count;
    for (const auto& d : music->documents) {
        if (d) out.documents.push_back(document_ref_from(*d));
    }
    return out;
}

}  // namespace cppgram
