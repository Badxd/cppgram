#include "cppgram/auth_state.h"

namespace cppgram {

std::optional<AuthState> next_auth_state(AuthState from, AuthEvent event) noexcept {
    // Disconnected is legal from every state, so shutdown during login always works.
    if (event == AuthEvent::Disconnected) return AuthState::Disconnected;

    switch (from) {
        case AuthState::Unauthenticated:
            switch (event) {
                case AuthEvent::ConnectStarted: return AuthState::Connecting;
                case AuthEvent::SessionRestored: return AuthState::Authorized;
                case AuthEvent::LoginStarted: return AuthState::Authenticating;
                case AuthEvent::SessionRevoked: return AuthState::AuthRevoked;
                case AuthEvent::TransientFailure: return AuthState::RecoverableError;
                default: return std::nullopt;
            }
        case AuthState::Connecting:
            switch (event) {
                case AuthEvent::Connected: return AuthState::Unauthenticated;
                case AuthEvent::TransientFailure: return AuthState::RecoverableError;
                case AuthEvent::PermanentFailure: return AuthState::PermanentError;
                default: return std::nullopt;
            }
        case AuthState::Authenticating:
            switch (event) {
                case AuthEvent::CodeRequested: return AuthState::CodeRequired;
                case AuthEvent::SignedIn: return AuthState::Authorized;
                case AuthEvent::PasswordNeeded: return AuthState::PasswordRequired;
                case AuthEvent::InvalidCode: return AuthState::CodeRequired;
                case AuthEvent::ExpiredCode: return AuthState::Unauthenticated;
                case AuthEvent::InvalidPassword: return AuthState::PasswordRequired;
                case AuthEvent::TransientFailure: return AuthState::RecoverableError;
                case AuthEvent::PermanentFailure: return AuthState::PermanentError;
                case AuthEvent::SessionRevoked: return AuthState::AuthRevoked;
                default: return std::nullopt;
            }
        case AuthState::CodeRequired:
            switch (event) {
                case AuthEvent::LoginStarted: return AuthState::Authenticating;
                case AuthEvent::ExpiredCode: return AuthState::Unauthenticated;
                case AuthEvent::PermanentFailure: return AuthState::PermanentError;
                default: return std::nullopt;
            }
        case AuthState::PasswordRequired:
            switch (event) {
                case AuthEvent::LoginStarted: return AuthState::Authenticating;
                case AuthEvent::PermanentFailure: return AuthState::PermanentError;
                default: return std::nullopt;
            }
        case AuthState::Authorized:
            switch (event) {
                case AuthEvent::ConnectStarted: return AuthState::Connecting;
                case AuthEvent::SessionExpired: return AuthState::AuthExpired;
                case AuthEvent::SessionRevoked: return AuthState::AuthRevoked;
                case AuthEvent::LoggedOut: return AuthState::Unauthenticated;
                case AuthEvent::TransientFailure: return AuthState::RecoverableError;
                default: return std::nullopt;
            }
        case AuthState::Disconnected:
            switch (event) {
                case AuthEvent::ConnectStarted: return AuthState::Connecting;
                default: return std::nullopt;
            }
        case AuthState::AuthExpired:
        case AuthState::AuthRevoked:
            switch (event) {
                case AuthEvent::LoggedOut: return AuthState::Unauthenticated;
                case AuthEvent::ConnectStarted: return AuthState::Connecting;
                default: return std::nullopt;
            }
        case AuthState::RecoverableError:
            switch (event) {
                case AuthEvent::ConnectStarted: return AuthState::Connecting;
                case AuthEvent::PermanentFailure: return AuthState::PermanentError;
                default: return std::nullopt;
            }
        case AuthState::PermanentError:
            // Terminal until the application disconnects (handled above) and starts over.
            return std::nullopt;
    }
    return std::nullopt;
}

namespace {
bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}
}  // namespace

AuthEvent classify_auth_error(std::int32_t code, const std::string& message) noexcept {
    if (starts_with(message, "FLOOD_WAIT_") || starts_with(message, "SLOWMODE_WAIT_")) {
        return AuthEvent::TransientFailure;
    }
    if (message == "PHONE_CODE_INVALID" || message == "PHONE_CODE_EMPTY") return AuthEvent::InvalidCode;
    if (message == "PHONE_CODE_EXPIRED") return AuthEvent::ExpiredCode;
    if (message == "SESSION_PASSWORD_NEEDED") return AuthEvent::PasswordNeeded;
    if (message == "PASSWORD_HASH_INVALID") return AuthEvent::InvalidPassword;
    if (message == "SESSION_EXPIRED") return AuthEvent::SessionExpired;
    if (message == "AUTH_KEY_UNREGISTERED" || message == "SESSION_REVOKED" || message == "AUTH_KEY_DUPLICATED") {
        return AuthEvent::SessionRevoked;
    }
    if (message == "USER_DEACTIVATED" || message == "USER_DEACTIVATED_BAN" || message == "PHONE_NUMBER_INVALID" ||
        message == "PHONE_NUMBER_BANNED" || message == "PHONE_NUMBER_FLOOD") {
        return AuthEvent::PermanentFailure;
    }
    if (code >= 500 && code < 600) return AuthEvent::TransientFailure;
    return AuthEvent::PermanentFailure;
}

bool auth_can_login(AuthState state) noexcept {
    return state == AuthState::Unauthenticated || state == AuthState::CodeRequired ||
           state == AuthState::PasswordRequired || state == AuthState::RecoverableError;
}

const char* to_string(AuthState state) noexcept {
    switch (state) {
        case AuthState::Unauthenticated: return "unauthenticated";
        case AuthState::Connecting: return "connecting";
        case AuthState::Authenticating: return "authenticating";
        case AuthState::CodeRequired: return "code_required";
        case AuthState::PasswordRequired: return "password_required";
        case AuthState::Authorized: return "authorized";
        case AuthState::Disconnected: return "disconnected";
        case AuthState::AuthExpired: return "auth_expired";
        case AuthState::AuthRevoked: return "auth_revoked";
        case AuthState::RecoverableError: return "recoverable_error";
        case AuthState::PermanentError: return "permanent_error";
    }
    return "unknown";
}

}  // namespace cppgram
