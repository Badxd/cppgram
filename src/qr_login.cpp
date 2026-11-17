#include "cppgram/qr_login.h"

#include <stdexcept>
#include <variant>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "password_settings.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

std::string base64url_encode(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((data.size() * 4 + 2) / 3);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }
    const std::size_t rest = data.size() - i;
    if (rest == 1) {
        const std::uint32_t v = std::uint32_t(data[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
    } else if (rest == 2) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
    }
    return out;
}

std::string qr_login_url(const std::vector<std::uint8_t>& token) {
    return "tg://login?token=" + base64url_encode(token);
}

namespace {

std::int64_t user_id_of(const std::shared_ptr<tl::User>& user) {
    if (!user) throw std::runtime_error("authorization has no user");
    if (const auto* full = std::get_if<ty::user>(&user->value)) return full->id;
    if (const auto* empty = std::get_if<ty::userEmpty>(&user->value)) return empty->id;
    throw std::runtime_error("unexpected user variant in authorization");
}

std::int64_t authorized_user(const tl::auth_Authorization& auth) {
    const auto* ok = std::get_if<ty::auth_authorization>(&auth.value);
    if (ok == nullptr) {
        // authorizationSignUpRequired: the phone number has no account. Sign-up is not implemented.
        throw std::runtime_error("QR login requires sign-up, which is not supported");
    }
    return user_id_of(ok->user);
}

} // namespace

LoginTokenInfo login_token_from_tl(const tl::auth_LoginToken& value) {
    LoginTokenInfo info;
    if (const auto* t = std::get_if<ty::auth_loginToken>(&value.value)) {
        info.state = LoginTokenState::Token;
        info.token = t->token;
        info.expires = t->expires;
    } else if (const auto* m = std::get_if<ty::auth_loginTokenMigrateTo>(&value.value)) {
        info.state = LoginTokenState::MigrateTo;
        info.token = m->token;
        info.dc_id = m->dc_id;
    } else if (const auto* s = std::get_if<ty::auth_loginTokenSuccess>(&value.value)) {
        if (!s->authorization) throw std::runtime_error("loginTokenSuccess without authorization");
        info.state = LoginTokenState::Success;
        info.authorized_user_id = authorized_user(*s->authorization);
    } else {
        throw std::runtime_error("unknown auth.LoginToken variant");
    }
    return info;
}

// ---- Client methods

LoginTokenInfo Client::export_login_token() {
    if (options_.api_id == 0 || options_.api_hash.empty()) {
        throw std::invalid_argument("QR login needs api_id and api_hash in ClientOptions");
    }
    fn::auth_exportLoginToken req;
    req.api_id = options_.api_id;
    req.api_hash = options_.api_hash;
    req.except_ids = {};
    return login_token_from_tl(call_rpc(req));
}

LoginTokenInfo Client::import_login_token(const std::vector<std::uint8_t>& token) {
    require_session();
    fn::auth_importLoginToken req;
    req.token = token;
    return login_token_from_tl(call_rpc(req));
}

AuthorizationExport Client::export_authorization(std::int32_t dc_id) {
    require_session();
    fn::auth_exportAuthorization req;
    req.dc_id = dc_id;
    const tl::auth_ExportedAuthorization res = call_rpc(req);
    const auto* ex = std::get_if<ty::auth_exportedAuthorization>(&res.value);
    if (ex == nullptr) throw std::runtime_error("unexpected auth.ExportedAuthorization variant");
    return AuthorizationExport{ex->id, ex->bytes};
}

std::int64_t Client::import_authorization(std::int64_t id, const std::vector<std::uint8_t>& bytes) {
    // Call this on a client connected to the target data center. This method does not switch data centers.
    fn::auth_importAuthorization req;
    req.id = id;
    req.bytes = bytes;
    return authorized_user(call_rpc(req));
}

// ---- sign-up and 2FA password recovery (auth.signUp, auth.requestPasswordRecovery, auth.recoverPassword)

std::int64_t Client::sign_up(const std::string& phone_number, const std::string& phone_code_hash,
                             const std::string& first_name, const std::string& last_name,
                             bool no_joined_notifications) {
    require_session();
    if (first_name.empty()) throw std::invalid_argument("sign_up needs a first name");
    fn::auth_signUp req;
    req.phone_number = phone_number;
    req.phone_code_hash = phone_code_hash;
    req.first_name = first_name;
    req.last_name = last_name;
    if (no_joined_notifications) req.no_joined_notifications = true;
    const std::int64_t id = authorized_user(call_rpc(req));
    apply_auth_event(AuthEvent::SignedIn);
    return id;
}

PasswordRecoveryInfo Client::request_password_recovery() {
    require_session();
    fn::auth_requestPasswordRecovery req;
    const auto res = call_rpc(req);
    const auto* pr = std::get_if<ty::auth_passwordRecovery>(&res.value);
    if (pr == nullptr) throw std::runtime_error("unexpected auth.PasswordRecovery variant");
    return PasswordRecoveryInfo{pr->email_pattern};
}

std::int64_t Client::recover_password(const std::string& code, const std::string& new_password,
                                      const std::string& hint) {
    require_session();
    if (code.empty()) throw std::invalid_argument("recover_password needs the emailed code");
    fn::auth_recoverPassword req;
    req.code = code;
    if (!new_password.empty()) {
        fn::account_getPassword get;
        const tl::account_Password result = call_rpc(get);
        const auto* pw = std::get_if<ty::account_password>(&result.value);
        if (pw == nullptr) throw std::runtime_error("unexpected account.getPassword response");
        req.new_settings = std::make_shared<tl::account_PasswordInputSettings>(
            tl::account_PasswordInputSettings{detail::make_new_password_settings(*pw, new_password, hint)});
    }
    const std::int64_t id = authorized_user(call_rpc(req));
    apply_auth_event(AuthEvent::SignedIn);
    return id;
}

} // namespace cppgram
