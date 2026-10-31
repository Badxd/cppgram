// User login flow: send code, sign in with the code, 2FA password, session probe.
// Every transition goes through cppgram/auth_state.h, so an out-of-order call fails with a clear error.
// Phone numbers, codes, hashes and passwords are never logged or put in error messages.
#include <stdexcept>
#include <variant>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

void Client::apply_auth_event(AuthEvent event) {
    const auto next = next_auth_state(auth_state_, event);
    if (!next) {
        throw std::logic_error(std::string("authorization event not allowed in state ") + to_string(auth_state_));
    }
    auth_state_ = *next;
}

AuthEvent Client::note_rpc_error(std::int32_t code, const std::string& message) noexcept {
    const AuthEvent event = classify_auth_error(code, message);
    if (const auto next = next_auth_state(auth_state_, event)) auth_state_ = *next;
    return event;
}

bool Client::probe_session() {
    require_session();
    if (auth_state_ == AuthState::Authorized) return true;
    fn::users_getUsers req;
    req.id = {std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}})};
    try {
        const auto users = session_->call(req);
        if (users.empty() || !users.front()) return false;
        apply_auth_event(AuthEvent::SessionRestored);
        return true;
    } catch (const mtproto::RpcError& e) {
        const AuthEvent event = note_rpc_error(e.code(), e.rpc_message());
        if (event == AuthEvent::SessionRevoked || event == AuthEvent::SessionExpired) return false;
        throw;
    }
}

LoginCodeInfo Client::send_login_code(const std::string& phone_number) {
    require_session();
    if (!auth_can_login(auth_state_)) {
        throw std::runtime_error(std::string("send_login_code not allowed in state ") + to_string(auth_state_));
    }
    apply_auth_event(AuthEvent::LoginStarted);

    fn::auth_sendCode req;
    req.phone_number = phone_number;
    req.api_id = options_.api_id;
    req.api_hash = options_.api_hash;
    req.settings = std::make_shared<tl::CodeSettings>(tl::CodeSettings{ty::codeSettings{}});

    tl::auth_SentCode res;
    try {
        res = session_->call(req);
    } catch (const mtproto::RpcError& e) {
        note_rpc_error(e.code(), e.rpc_message());
        throw;
    }

    LoginCodeInfo info;
    if (std::holds_alternative<ty::auth_sentCodeSuccess>(res.value)) {
        // Telegram accepted the account without a code (for example an existing authorization).
        apply_auth_event(AuthEvent::SignedIn);
        info.delivery = "authorized";
        return info;
    }
    if (std::holds_alternative<ty::auth_sentCodePaymentRequired>(res.value)) {
        apply_auth_event(AuthEvent::PermanentFailure);
        throw std::runtime_error("auth.sendCode requires a payment (store product); cppgram cannot complete it");
    }
    const auto& sent = std::get<ty::auth_sentCode>(res.value);
    info.phone_code_hash = sent.phone_code_hash;
    if (sent.timeout) info.timeout_seconds = *sent.timeout;
    info.delivery = "other";
    if (sent.type) {
        const auto& t = sent.type->value;
        if (std::holds_alternative<ty::auth_sentCodeTypeApp>(t)) info.delivery = "app";
        else if (std::holds_alternative<ty::auth_sentCodeTypeSms>(t)) info.delivery = "sms";
        else if (std::holds_alternative<ty::auth_sentCodeTypeCall>(t)) info.delivery = "call";
        else if (std::holds_alternative<ty::auth_sentCodeTypeFlashCall>(t)) info.delivery = "flash_call";
        else if (std::holds_alternative<ty::auth_sentCodeTypeEmailCode>(t)) info.delivery = "email";
    }
    apply_auth_event(AuthEvent::CodeRequested);
    return info;
}

bool Client::sign_in_code(const std::string& phone_number, const std::string& code,
                          const std::string& phone_code_hash) {
    require_session();
    if (auth_state_ != AuthState::CodeRequired) {
        throw std::runtime_error(std::string("sign_in_code needs send_login_code first; state is ") +
                                 to_string(auth_state_));
    }
    apply_auth_event(AuthEvent::LoginStarted);

    fn::auth_signIn req;
    req.phone_number = phone_number;
    req.phone_code_hash = phone_code_hash;
    req.phone_code = code;
    try {
        const tl::auth_Authorization res = session_->call(req);
        if (std::holds_alternative<ty::auth_authorizationSignUpRequired>(res.value)) {
            apply_auth_event(AuthEvent::PermanentFailure);
            throw std::runtime_error("this phone number has no Telegram account; sign-up is not supported");
        }
        apply_auth_event(AuthEvent::SignedIn);
        return true;
    } catch (const mtproto::RpcError& e) {
        // SESSION_PASSWORD_NEEDED is a normal step: the account has 2FA, so report false and move on.
        if (note_rpc_error(e.code(), e.rpc_message()) == AuthEvent::PasswordNeeded) return false;
        throw;
    }
}

bool Client::sign_in_password(const std::string& password) {
    require_session();
    if (auth_state_ != AuthState::PasswordRequired) {
        throw std::runtime_error(std::string("sign_in_password needs a 2FA challenge first; state is ") +
                                 to_string(auth_state_));
    }
    apply_auth_event(AuthEvent::LoginStarted);
    try {
        check_password(password);
    } catch (const mtproto::RpcError& e) {
        // Wrong password: state goes back to PasswordRequired and the RPC error is rethrown unchanged.
        note_rpc_error(e.code(), e.rpc_message());
        throw;
    }
    apply_auth_event(AuthEvent::SignedIn);
    return true;
}

}  // namespace cppgram
