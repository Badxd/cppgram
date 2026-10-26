#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

// ---- phase 9: account settings

std::int32_t Client::get_account_ttl_days() {
    require_session();
    fn::account_getAccountTTL req;
    const tl::AccountDaysTTL res = session_->call(req);
    const auto* d = std::get_if<ty::accountDaysTTL>(&res.value);
    if (d == nullptr) throw std::runtime_error("get_account_ttl_days: unexpected response");
    return d->days;
}

void Client::set_account_ttl_days(std::int32_t days) {
    require_session();
    fn::account_setAccountTTL req;
    req.ttl = std::make_shared<tl::AccountDaysTTL>(tl::AccountDaysTTL{ty::accountDaysTTL{days}});
    session_->call(req);
}

bool Client::check_username(const std::string& username) {
    require_session();
    fn::account_checkUsername req;
    req.username = username;
    return session_->call(req);
}

GlobalPrivacyInfo Client::get_global_privacy_settings() {
    require_session();
    fn::account_getGlobalPrivacySettings req;
    const tl::GlobalPrivacySettings res = session_->call(req);
    const auto* g = std::get_if<ty::globalPrivacySettings>(&res.value);
    if (g == nullptr) throw std::runtime_error("get_global_privacy_settings: unexpected response");
    GlobalPrivacyInfo out;
    out.archive_and_mute_new_noncontact_peers = g->archive_and_mute_new_noncontact_peers;
    out.keep_archived_unmuted = g->keep_archived_unmuted;
    out.keep_archived_folders = g->keep_archived_folders;
    out.new_noncontact_peers_require_premium = g->new_noncontact_peers_require_premium;
    out.display_gifts_button = g->display_gifts_button;
    return out;
}

void Client::set_archive_and_mute_new_noncontact(bool enabled) {
    require_session();
    const GlobalPrivacyInfo current = get_global_privacy_settings();
    ty::globalPrivacySettings next{};
    next.archive_and_mute_new_noncontact_peers = enabled;
    next.keep_archived_unmuted = current.keep_archived_unmuted;
    next.keep_archived_folders = current.keep_archived_folders;
    next.new_noncontact_peers_require_premium = current.new_noncontact_peers_require_premium;
    next.display_gifts_button = current.display_gifts_button;
    fn::account_setGlobalPrivacySettings req;
    req.settings = std::make_shared<tl::GlobalPrivacySettings>(tl::GlobalPrivacySettings{next});
    session_->call(req);
}

// ---- phase 10: emoji and recent stickers

void Client::clear_recent_stickers(bool attached) {
    require_session();
    fn::messages_clearRecentStickers req;
    req.attached = attached;
    session_->call(req);
}

std::vector<std::int64_t> Client::get_default_emoji_status_ids() {
    require_session();
    fn::account_getDefaultEmojiStatuses req;
    req.hash = 0;
    const tl::account_EmojiStatuses res = session_->call(req);
    std::vector<std::int64_t> out;
    const auto* list = std::get_if<ty::account_emojiStatuses>(&res.value);
    if (list == nullptr) return out;
    for (const auto& s : list->statuses) {
        if (!s) continue;
        if (const auto* st = std::get_if<ty::emojiStatus>(&s->value)) out.push_back(st->document_id);
    }
    return out;
}

std::vector<std::int64_t> Client::get_custom_emoji_document_ids(const std::vector<std::int64_t>& ids) {
    require_session();
    fn::messages_getCustomEmojiDocuments req;
    req.document_id = ids;
    const auto docs = session_->call(req);
    std::vector<std::int64_t> out;
    for (const auto& d : docs) {
        if (!d) continue;
        if (const auto* doc = std::get_if<ty::document>(&d->value)) out.push_back(doc->id);
    }
    return out;
}

}  // namespace cppgram
