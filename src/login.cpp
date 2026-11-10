// User login flow: send code, sign in with the code, 2FA password, session probe.
// Every transition goes through cppgram/auth_state.h, so an out-of-order call fails with a clear error.
// Phone numbers, codes, hashes and passwords are never logged or put in error messages.
#include <stdexcept>
#include <variant>

#include "cppgram/client.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/errors.h"
#include "cppgram/log.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

// Maps an auth.sentCode to the public result. Shared by send_login_code and resend_login_code.
LoginCodeInfo sent_code_info(const ty::auth_sentCode& sent) {
    LoginCodeInfo info;
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
    return info;
}

} // namespace


void Client::apply_auth_event(AuthEvent event) {
    const auto next = next_auth_state(auth_state_, event);
    if (!next) {
        throw std::logic_error(std::string("authorization event not allowed in state ") + to_string(auth_state_));
    }
    set_auth_state(*next);
}

// A login step failed for a reason other than an RPC reply (a connection error, or an unknown migration DC). The
// pending code is no longer known to be usable, so the next call must start the login again. If the connection
// was lost, connect() has already set Disconnected, and that state is kept.
void Client::recover_login_state() noexcept {
    if (connected_.load(std::memory_order_acquire)) set_auth_state(AuthState::RecoverableError);
}

AuthEvent Client::note_rpc_error(std::int32_t code, const std::string& message) noexcept {
    const AuthEvent event = classify_auth_error(code, message);
    if (const auto next = next_auth_state(auth_state_, event)) set_auth_state(*next);
    return event;
}

bool Client::probe_session() {
    require_session();
    if (auth_state_ == AuthState::Authorized) return true;
    fn::users_getUsers req;
    req.id = {std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}})};
    try {
        const auto users = call_rpc(req);
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
        throw errors::AuthenticationError(std::string("send_login_code not allowed in state ") + to_string(auth_state_));
    }
    // A new code supersedes any earlier one. login_dc_ is set again only when this request succeeds.
    login_dc_ = 0;
    apply_auth_event(AuthEvent::LoginStarted);

    fn::auth_sendCode req;
    req.phone_number = phone_number;
    req.api_id = options_.api_id;
    req.api_hash = options_.api_hash;
    req.settings = std::make_shared<tl::CodeSettings>(tl::CodeSettings{ty::codeSettings{}});

    tl::auth_SentCode res;
    try {
        res = call_login_rpc(req);
    } catch (const mtproto::RpcError& e) {
        note_rpc_error(e.code(), e.rpc_message());
        throw;
    } catch (...) {
        recover_login_state();
        throw;
    }

    log::write(log::Level::Info, "login", "login code requested");
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
    info = sent_code_info(std::get<ty::auth_sentCode>(res.value));
    login_dc_ = options_.dc_id;
    apply_auth_event(AuthEvent::CodeRequested);
    return info;
}

bool Client::sign_in_code(const std::string& phone_number, const std::string& code,
                          const std::string& phone_code_hash) {
    require_session();
    if (auth_state_ != AuthState::CodeRequired) {
        throw errors::AuthenticationError(std::string("sign_in_code needs send_login_code first; state is ") +
                                 to_string(auth_state_));
    }
    if (login_dc_ == 0) {
        throw errors::AuthenticationError("sign_in_code has no DC for the phone code; call send_login_code again");
    }
    // The hash is valid only on the DC that issued it. If the connection moved away (for example to another DC
    // after an unrelated migration), go back there before the code is submitted. The code state is kept.
    if (options_.dc_id != login_dc_) migrate_to_dc(login_dc_);
    apply_auth_event(AuthEvent::LoginStarted);

    fn::auth_signIn req;
    req.phone_number = phone_number;
    req.phone_code_hash = phone_code_hash;
    req.phone_code = code;
    try {
        tl::auth_Authorization res;
        try {
            res = call_rpc(req);
        } catch (const mtproto::RpcError& e) {
            const std::int32_t dc = migrate_dc_from_error(e.rpc_message());
            if (dc != 0) {
                // The account is on another DC. The code was issued by login_dc_, so it is not valid on dc. It is
                // not sent there. The pending code is dropped and the login must start again with send_login_code.
                const std::int32_t issued_by = login_dc_;
                login_dc_ = 0;
                log::write(log::Level::Warn, "login", "sign-in asked for another DC; pending code dropped",
                           {{"issued_by", std::to_string(issued_by)}, {"account_dc", std::to_string(dc)}});
                throw errors::AuthenticationError("the account is on DC " + std::to_string(dc) +
                                                  " but the code was issued by DC " + std::to_string(issued_by) +
                                                  "; call send_login_code again");
            }
            throw;
        }
        if (std::holds_alternative<ty::auth_authorizationSignUpRequired>(res.value)) {
            apply_auth_event(AuthEvent::PermanentFailure);
            log::write(log::Level::Warn, "login", "sign-up required; not supported");
            instruments_.auth_failures.inc();
            throw std::runtime_error("this phone number has no Telegram account; sign-up is not supported");
        }
        login_dc_ = 0;
        apply_auth_event(AuthEvent::SignedIn);
        log::write(log::Level::Info, "login", "signed in");
        return true;
    } catch (const mtproto::RpcError& e) {
        // SESSION_PASSWORD_NEEDED is a normal step: the account has 2FA, so report false and move on. The 2FA
        // challenge belongs to the same DC, so login_dc_ is kept for the password step.
        if (note_rpc_error(e.code(), e.rpc_message()) == AuthEvent::PasswordNeeded) {
            log::write(log::Level::Info, "login", "2FA password required");
            return false;
        }
        log::write(log::Level::Warn, "login", "sign-in rejected", {{"code", std::to_string(e.code())}});
        instruments_.auth_failures.inc();
        throw;
    } catch (...) {
        recover_login_state();
        throw;
    }
}

bool Client::sign_in_password(const std::string& password) {
    require_session();
    if (auth_state_ != AuthState::PasswordRequired) {
        throw errors::AuthenticationError(std::string("sign_in_password needs a 2FA challenge first; state is ") +
                                 to_string(auth_state_));
    }
    // The 2FA challenge belongs to the DC that accepted the sign-in (login_dc_). Go back there first.
    if (login_dc_ != 0 && options_.dc_id != login_dc_) migrate_to_dc(login_dc_);
    apply_auth_event(AuthEvent::LoginStarted);
    try {
        check_password(password);
    } catch (const mtproto::RpcError& e) {
        // Wrong password: state goes back to PasswordRequired and the RPC error is rethrown unchanged.
        log::write(log::Level::Warn, "login", "2FA password rejected", {{"code", std::to_string(e.code())}});
        instruments_.auth_failures.inc();
        note_rpc_error(e.code(), e.rpc_message());
        throw;
    } catch (...) {
        recover_login_state();
        throw;
    }
    login_dc_ = 0;
    apply_auth_event(AuthEvent::SignedIn);
    log::write(log::Level::Info, "login", "signed in with 2FA password");
    return true;
}

LoginCodeInfo Client::resend_login_code(const std::string& phone_number, const std::string& phone_code_hash,
                                        const std::string& reason) {
    require_session();
    if (phone_code_hash.empty()) throw std::invalid_argument("resend_login_code needs the phone_code_hash from send_login_code");
    // A pending login belongs to login_dc_, so the resend goes to that DC. The new hash is recorded for the DC that
    // answers. Without a pending login the request runs on the current DC, as before.
    const bool pending = login_dc_ != 0;
    if (pending && options_.dc_id != login_dc_) migrate_to_dc(login_dc_);
    fn::auth_resendCode req;
    req.phone_number = phone_number;
    req.phone_code_hash = phone_code_hash;
    if (!reason.empty()) req.reason = reason;
    tl::auth_SentCode res;
    try {
        res = call_login_rpc(req);
    } catch (const mtproto::RpcError& e) {
        note_rpc_error(e.code(), e.rpc_message());
        throw;
    }
    log::write(log::Level::Info, "login", "login code resent");
    if (std::holds_alternative<ty::auth_sentCodeSuccess>(res.value)) {
        login_dc_ = 0;
        apply_auth_event(AuthEvent::SignedIn);
        LoginCodeInfo info;
        info.delivery = "authorized";
        return info;
    }
    if (std::holds_alternative<ty::auth_sentCodePaymentRequired>(res.value)) {
        throw std::runtime_error("auth.resendCode requires a payment (store product); cppgram cannot complete it");
    }
    // The new hash is issued by the DC that answered, which is the current one after any move.
    if (pending) login_dc_ = options_.dc_id;
    return sent_code_info(std::get<ty::auth_sentCode>(res.value));
}

}  // namespace cppgram
