#include "cppgram/client.h"

#include <stdexcept>
#include <variant>

#include "cppgram/mtproto/srp.h"
#include "cppgram/session_string.h"
#include "cppgram/storage.h"
#include "password_settings.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputUser> self_input_user() {
    return std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}});
}

}  // namespace

std::string Client::export_session_string() const {
    mtproto::SessionData data;
    const std::string& path = options_.session_path;
    const bool sqlite_path = path.ends_with(".sqlite") || path.ends_with(".sqlite3") || path.ends_with(".db");
    bool ok = false;
    if (sqlite_path) {
        SqliteStorage storage(path);
        ok = storage.load_session(data);
    } else {
        ok = mtproto::load_session_file(path, data);
    }
    if (!ok || data.auth_key.size() != 256) {
        throw std::runtime_error("no saved session to export; connect and log in first");
    }
    return encode_session_string(data);
}

bool Client::set_bot_info(const std::string& lang_code, const std::string& name, const std::string& about,
                          const std::string& description) {
    require_session();
    fn::bots_setBotInfo req;
    req.lang_code = lang_code;
    if (!name.empty()) req.name = name;
    if (!about.empty()) req.about = about;
    if (!description.empty()) req.description = description;
    return call_rpc(req);
}

BotInfoText Client::get_bot_info(const std::string& lang_code) {
    require_session();
    fn::bots_getBotInfo req;
    req.lang_code = lang_code;
    const tl::bots_BotInfo result = call_rpc(req);
    BotInfoText out;
    if (const auto* info = std::get_if<ty::bots_botInfo>(&result.value)) {
        out.name = info->name;
        out.about = info->about;
        out.description = info->description;
    }
    return out;
}

bool Client::set_default_menu_button() {
    require_session();
    fn::bots_setBotMenuButton req;
    req.user_id = self_input_user();
    req.button = std::make_shared<tl::BotMenuButton>(tl::BotMenuButton{ty::botMenuButtonDefault{}});
    return call_rpc(req);
}

}  // namespace cppgram

namespace cppgram {

namespace {

std::shared_ptr<tl::InputPrivacyKey> privacy_key(PrivacyKey key) {
    switch (key) {
        case PrivacyKey::LastSeen:
            return std::make_shared<tl::InputPrivacyKey>(tl::InputPrivacyKey{ty::inputPrivacyKeyStatusTimestamp{}});
        case PrivacyKey::ProfilePhoto:
            return std::make_shared<tl::InputPrivacyKey>(tl::InputPrivacyKey{ty::inputPrivacyKeyProfilePhoto{}});
        case PrivacyKey::PhoneNumber:
            return std::make_shared<tl::InputPrivacyKey>(tl::InputPrivacyKey{ty::inputPrivacyKeyPhoneNumber{}});
        case PrivacyKey::Forwards:
            return std::make_shared<tl::InputPrivacyKey>(tl::InputPrivacyKey{ty::inputPrivacyKeyForwards{}});
        case PrivacyKey::Invite:
            return std::make_shared<tl::InputPrivacyKey>(tl::InputPrivacyKey{ty::inputPrivacyKeyChatInvite{}});
    }
    throw std::invalid_argument("unknown privacy key");
}

}  // namespace

void Client::set_privacy(PrivacyKey key, PrivacyLevel level) {
    require_session();
    fn::account_setPrivacy req;
    req.key = privacy_key(key);
    std::shared_ptr<tl::InputPrivacyRule> rule;
    switch (level) {
        case PrivacyLevel::Everyone:
            rule = std::make_shared<tl::InputPrivacyRule>(tl::InputPrivacyRule{ty::inputPrivacyValueAllowAll{}});
            break;
        case PrivacyLevel::Contacts:
            rule = std::make_shared<tl::InputPrivacyRule>(tl::InputPrivacyRule{ty::inputPrivacyValueAllowContacts{}});
            break;
        case PrivacyLevel::Nobody:
            rule = std::make_shared<tl::InputPrivacyRule>(tl::InputPrivacyRule{ty::inputPrivacyValueDisallowAll{}});
            break;
    }
    req.rules.push_back(rule);
    call_rpc(req);
}

PrivacyLevel Client::get_privacy(PrivacyKey key) {
    require_session();
    fn::account_getPrivacy req;
    req.key = privacy_key(key);
    const tl::account_PrivacyRules result = call_rpc(req);
    const auto* rules = std::get_if<ty::account_privacyRules>(&result.value);
    if (rules == nullptr) return PrivacyLevel::Nobody;
    bool contacts = false;
    for (const auto& r : rules->rules) {
        if (!r) continue;
        if (std::holds_alternative<ty::privacyValueAllowAll>(r->value)) return PrivacyLevel::Everyone;
        if (std::holds_alternative<ty::privacyValueAllowContacts>(r->value)) contacts = true;
    }
    return contacts ? PrivacyLevel::Contacts : PrivacyLevel::Nobody;
}

}  // namespace cppgram

namespace cppgram {

namespace {

tl::account_Password fetch_password_settings(mtproto::MtSession& session) {
    fn::account_getPassword req;
    return session.call(req);
}

// Builds the SRP proof of the current password from account.getPassword data.
tl::InputCheckPasswordSRP make_password_check(const ty::account_password& pw, const std::string& password) {
    if (!pw.current_algo || !*pw.current_algo) {
        throw std::runtime_error("this account has no two-step verification password");
    }
    const auto* algo = std::get_if<ty::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
        &(*pw.current_algo)->value);
    if (algo == nullptr) throw std::runtime_error("server uses an unsupported password KDF algorithm");
    if (!pw.srp_B || !pw.srp_id) throw std::runtime_error("server sent no SRP parameters");

    mtproto::SrpParams params;
    params.salt1 = algo->salt1;
    params.salt2 = algo->salt2;
    params.g = algo->g;
    params.p = algo->p;
    params.server_b = *pw.srp_B;
    const mtproto::SrpCheck check = mtproto::compute_srp_check(
        mtproto::Bytes(password.begin(), password.end()), params, mtproto::random_bytes(256));

    ty::inputCheckPasswordSRP in;
    in.srp_id = *pw.srp_id;
    in.A = check.A;
    in.M1 = check.M1;
    return tl::InputCheckPasswordSRP{in};
}

}  // namespace

PasswordInfo Client::get_password_info() {
    require_session();
    const tl::account_Password result = fetch_password_settings(*session_);
    PasswordInfo out;
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) return out;
    out.has_password = pw->current_algo.has_value() && static_cast<bool>(*pw->current_algo);
    if (pw->hint) out.hint = *pw->hint;
    return out;
}

bool Client::check_password(const std::string& password) {
    require_session();
    const tl::account_Password result = fetch_password_settings(*session_);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
    fn::auth_checkPassword req;
    req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, password));
    call_rpc(req);
    return true;
}

bool Client::set_password(const std::string& current_password, const std::string& new_password,
                          const std::string& hint) {
    require_session();
    if (new_password.empty()) throw std::invalid_argument("set_password: new password must not be empty");
    const tl::account_Password result = fetch_password_settings(*session_);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
    const ty::account_passwordInputSettings settings = detail::make_new_password_settings(*pw, new_password, hint);

    fn::account_updatePasswordSettings req;
    if (pw->current_algo && *pw->current_algo) {
        if (current_password.empty()) {
            throw std::invalid_argument("set_password: this account has a password, current_password is required");
        }
        req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, current_password));
    }
    req.new_settings = std::make_shared<tl::account_PasswordInputSettings>(tl::account_PasswordInputSettings{settings});
    return call_rpc(req);
}

bool Client::remove_password(const std::string& current_password) {
    require_session();
    const tl::account_Password result = fetch_password_settings(*session_);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr || !pw->current_algo || !*pw->current_algo) return true;  // nothing to remove

    ty::account_passwordInputSettings settings;
    settings.new_algo = std::make_shared<tl::PasswordKdfAlgo>(tl::PasswordKdfAlgo{ty::passwordKdfAlgoUnknown{}});
    settings.new_password_hash = mtproto::Bytes{};
    settings.hint = std::string{};

    fn::account_updatePasswordSettings req;
    req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, current_password));
    req.new_settings = std::make_shared<tl::account_PasswordInputSettings>(tl::account_PasswordInputSettings{settings});
    return call_rpc(req);
}

}  // namespace cppgram

namespace cppgram {

namespace {

DocumentRef document_ref_of(const tl::Document& d) {
    DocumentRef out;
    if (const auto* doc = std::get_if<ty::document>(&d.value)) {
        out.id = doc->id;
        out.access_hash = doc->access_hash;
        out.file_reference = doc->file_reference;
    }
    return out;
}

}  // namespace

bool Client::set_favorite_sticker(const DocumentRef& sticker, bool favorite) {
    require_session();
    fn::messages_faveSticker req;
    req.id = std::make_shared<tl::InputDocument>(
        tl::InputDocument{ty::inputDocument{sticker.id, sticker.access_hash, sticker.file_reference}});
    req.unfave = !favorite;
    return call_rpc(req);
}

bool Client::set_favorite_sticker(const Message& sticker, bool favorite) {
    if (sticker.media.kind != MediaKind::Document) {
        throw std::invalid_argument("set_favorite_sticker: message does not contain a document");
    }
    DocumentRef ref;
    ref.id = sticker.media.id;
    ref.access_hash = sticker.media.access_hash;
    ref.file_reference = sticker.media.file_reference;
    return set_favorite_sticker(ref, favorite);
}

std::vector<DocumentRef> Client::get_favorite_stickers() {
    require_session();
    fn::messages_getFavedStickers req;
    req.hash = 0;
    const tl::messages_FavedStickers result = call_rpc(req);
    std::vector<DocumentRef> out;
    if (const auto* list = std::get_if<ty::messages_favedStickers>(&result.value)) {
        for (const auto& d : list->stickers) {
            if (d) out.push_back(document_ref_of(*d));
        }
    }
    return out;
}

UploadedFile Client::save_file(const std::string& path) {
    return save_file(path, UploadProgress{});
}

UploadedFile Client::save_file(const std::string& path, UploadProgress progress) {
    require_session();
    UploadedFile out;
    out.handle = upload(path, out.name, out.size, progress);
    return out;
}

async::AsyncResult<UploadedFile> Client::save_file_async(const std::string& path, UploadProgress progress,
                                                         std::stop_token stop) {
    auto result = async([path, progress](Client& client, std::stop_token) { return client.save_file(path, progress); });
    result.link(std::move(stop));
    return result;
}

bool Client::set_menu_button_url(const std::string& text, const std::string& url) {
    require_session();
    if (text.empty() || url.empty()) throw std::invalid_argument("set_menu_button_url: text and url are required");
    fn::bots_setBotMenuButton req;
    req.user_id = self_input_user();
    req.button = std::make_shared<tl::BotMenuButton>(tl::BotMenuButton{ty::botMenuButton{text, url}});
    return call_rpc(req);
}

}  // namespace cppgram

namespace cppgram {

WalletStateInfo Client::replace_wallet(const WalletReplacement& replacement, const std::string& password) {
    require_session();
    fn::wallet_replaceWallet req;
    if (replacement.kind == WalletReplacement::Kind::New) {
        req.wallet = std::make_shared<tl::InputWalletReplacement>(tl::InputWalletReplacement{ty::inputWalletNew{}});
    } else {
        if (replacement.public_key.empty()) throw std::invalid_argument("imported wallet needs a public key");
        ty::inputWalletImported w;
        w.public_key = replacement.public_key;
        if (replacement.anchor_public_key) {
            w.anchor_public_key = *replacement.anchor_public_key;
        }
        ty::walletOwnershipProof proof;
        proof.timestamp = replacement.proof.timestamp;
        proof.signature = replacement.proof.signature;
        w.proof = std::make_shared<tl::WalletOwnershipProof>(tl::WalletOwnershipProof{proof});
        req.wallet = std::make_shared<tl::InputWalletReplacement>(tl::InputWalletReplacement{w});
    }
    if (!password.empty()) {
        const tl::account_Password result = fetch_password_settings(*session_);
        const auto* pw = std::get_if<ty::account_password>(&result.value);
        if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
        req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, password));
    }
    const tl::WalletState state = call_rpc(req);
    WalletStateInfo out;
    if (const auto* e = std::get_if<ty::walletStateEmpty>(&state.value)) {
        out.empty = true;
        out.creating = e->creating;
        return out;
    }
    const auto* ws = std::get_if<ty::walletState>(&state.value);
    if (ws == nullptr) throw std::runtime_error("unexpected WalletState variant");
    out.empty = false;
    out.backup_enabled = ws->backup_enabled;
    out.can_export_phrase = ws->can_export_phrase;
    out.can_enable_backup = ws->can_enable_backup;
    out.address = ws->address;
    out.public_key = ws->public_key;
    out.balance = ws->balance;
    return out;
}

}  // namespace cppgram

namespace cppgram {

TmpPasswordInfo Client::get_tmp_password(const std::string& password, std::int32_t period_seconds) {
    require_session();
    if (password.empty()) throw std::invalid_argument("get_tmp_password needs the 2FA password");
    if (period_seconds <= 0) throw std::invalid_argument("get_tmp_password period must be positive");
    const tl::account_Password result = fetch_password_settings(*session_);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
    fn::account_getTmpPassword req;
    req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, password));
    req.period = period_seconds;
    const tl::account_TmpPassword tmp = call_rpc(req);
    const auto* t = std::get_if<ty::account_tmpPassword>(&tmp.value);
    if (t == nullptr) throw std::runtime_error("unexpected account.TmpPassword variant");
    TmpPasswordInfo out;
    out.tmp_password = t->tmp_password;
    out.valid_until = t->valid_until;
    return out;
}

}  // namespace cppgram

namespace cppgram {

bool Client::edit_chat_creator(const PeerRef& peer, const PeerRef& user, const std::string& password) {
    require_session();
    if (password.empty()) throw std::invalid_argument("edit_chat_creator needs the 2FA password");
    if (user.kind != PeerKind::User || !user.has_access_hash) {
        throw std::invalid_argument("edit_chat_creator needs a user peer with an access hash");
    }
    const tl::account_Password result = fetch_password_settings(*session_);
    const auto* pw = std::get_if<ty::account_password>(&result.value);
    if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
    fn::messages_editChatCreator req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
    req.user_id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{user.id, user.access_hash}});
    req.password = std::make_shared<tl::InputCheckPasswordSRP>(make_password_check(*pw, password));
    (void)call_rpc(req);
    return true;
}

}  // namespace cppgram
