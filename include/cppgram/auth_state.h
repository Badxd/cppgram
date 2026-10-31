// cppgram - explicit authorization state machine.
// Pure functions: the transition table and the RPC-error mapping can be tested without a server.
// The Client owns one AuthState and applies events from its login methods.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cppgram {

enum class AuthState {
    Unauthenticated,   // connected (or not yet), no authorization on this session
    Connecting,        // transport being (re)established
    Authenticating,    // a login RPC is in flight
    CodeRequired,      // auth.sendCode succeeded; waiting for the login code
    PasswordRequired,  // account has 2FA; waiting for the password
    Authorized,        // session is logged in
    Disconnected,      // closed by the application (safe shutdown lands here)
    AuthExpired,       // server reports the session expired; a new login is needed
    AuthRevoked,       // session was terminated elsewhere or the key is unregistered
    RecoverableError,  // transient failure (network, flood wait, server error); retry allowed
    PermanentError,    // input or account problem that retrying will not fix
};

enum class AuthEvent {
    ConnectStarted,      // Unauthenticated/Disconnected/RecoverableError -> Connecting
    Connected,           // transport is up; no authorization known yet
    SessionRestored,     // users.getUsers(self) succeeded on an existing key
    CodeRequested,       // auth.sendCode succeeded
    LoginStarted,        // auth.signIn or auth.checkPassword sent
    SignedIn,            // login accepted
    PasswordNeeded,      // SESSION_PASSWORD_NEEDED
    InvalidCode,         // PHONE_CODE_INVALID / PHONE_CODE_EMPTY: may retry the code
    ExpiredCode,         // PHONE_CODE_EXPIRED: must request a new code
    InvalidPassword,     // PASSWORD_HASH_INVALID: may retry the password
    TransientFailure,    // network or server-side failure: retry later
    PermanentFailure,    // wrong phone number, deactivated account, or other non-retryable error
    SessionExpired,      // SESSION_EXPIRED
    SessionRevoked,      // AUTH_KEY_UNREGISTERED / SESSION_REVOKED
    LoggedOut,           // auth.logOut succeeded
    Disconnected,        // application closed the client; legal from every state
};

// Returns the next state, or std::nullopt when the event is not allowed in `from`.
std::optional<AuthState> next_auth_state(AuthState from, AuthEvent event) noexcept;

// Maps a Telegram RPC error (code and message name, e.g. 400 "PHONE_CODE_INVALID") to an event.
// FLOOD_WAIT_n maps to TransientFailure. Unknown 5xx errors are transient; unknown 4xx are permanent.
AuthEvent classify_auth_error(std::int32_t code, const std::string& message) noexcept;

// Whether a state allows a new login attempt (send code or sign in).
bool auth_can_login(AuthState state) noexcept;

const char* to_string(AuthState state) noexcept;

}  // namespace cppgram
