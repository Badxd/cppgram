#include <gtest/gtest.h>

#include <array>

#include "cppgram/auth_state.h"

using namespace cppgram;

namespace {
constexpr std::array<AuthState, 11> kAllStates = {
    AuthState::Unauthenticated, AuthState::Connecting,     AuthState::Authenticating, AuthState::CodeRequired,
    AuthState::PasswordRequired, AuthState::Authorized,    AuthState::Disconnected,   AuthState::AuthExpired,
    AuthState::AuthRevoked,     AuthState::RecoverableError, AuthState::PermanentError,
};
}  // namespace

TEST(AuthState, LoginPathReachesAuthorized) {
    auto s = AuthState::Unauthenticated;
    s = *next_auth_state(s, AuthEvent::LoginStarted);
    EXPECT_EQ(s, AuthState::Authenticating);
    s = *next_auth_state(s, AuthEvent::CodeRequested);
    EXPECT_EQ(s, AuthState::CodeRequired);
    s = *next_auth_state(s, AuthEvent::LoginStarted);
    s = *next_auth_state(s, AuthEvent::PasswordNeeded);
    EXPECT_EQ(s, AuthState::PasswordRequired);
    s = *next_auth_state(s, AuthEvent::LoginStarted);
    s = *next_auth_state(s, AuthEvent::SignedIn);
    EXPECT_EQ(s, AuthState::Authorized);
}

TEST(AuthState, WrongCodeKeepsCodeRequiredAndAllowsRetry) {
    auto s = *next_auth_state(AuthState::Authenticating, AuthEvent::InvalidCode);
    EXPECT_EQ(s, AuthState::CodeRequired);
    EXPECT_TRUE(auth_can_login(s));
}

TEST(AuthState, ExpiredCodeReturnsToUnauthenticated) {
    EXPECT_EQ(*next_auth_state(AuthState::Authenticating, AuthEvent::ExpiredCode), AuthState::Unauthenticated);
    EXPECT_EQ(*next_auth_state(AuthState::CodeRequired, AuthEvent::ExpiredCode), AuthState::Unauthenticated);
}

TEST(AuthState, WrongPasswordStaysPasswordRequired) {
    EXPECT_EQ(*next_auth_state(AuthState::Authenticating, AuthEvent::InvalidPassword), AuthState::PasswordRequired);
}

TEST(AuthState, DisconnectIsLegalFromEveryState) {
    for (auto s : kAllStates) {
        auto next = next_auth_state(s, AuthEvent::Disconnected);
        ASSERT_TRUE(next.has_value()) << to_string(s);
        EXPECT_EQ(*next, AuthState::Disconnected) << to_string(s);
    }
}

TEST(AuthState, RevokedAndExpiredSessionsNeedALoginAfterLogout) {
    EXPECT_EQ(*next_auth_state(AuthState::Authorized, AuthEvent::SessionRevoked), AuthState::AuthRevoked);
    EXPECT_EQ(*next_auth_state(AuthState::Authorized, AuthEvent::SessionExpired), AuthState::AuthExpired);
    EXPECT_EQ(*next_auth_state(AuthState::AuthRevoked, AuthEvent::LoggedOut), AuthState::Unauthenticated);
    EXPECT_FALSE(next_auth_state(AuthState::AuthRevoked, AuthEvent::SignedIn).has_value());
    EXPECT_FALSE(auth_can_login(AuthState::AuthRevoked));
}

TEST(AuthState, PermanentErrorIsTerminalExceptForDisconnect) {
    for (auto e : {AuthEvent::ConnectStarted, AuthEvent::LoginStarted, AuthEvent::SignedIn,
                   AuthEvent::TransientFailure, AuthEvent::PermanentFailure}) {
        EXPECT_FALSE(next_auth_state(AuthState::PermanentError, e).has_value());
    }
    EXPECT_FALSE(auth_can_login(AuthState::PermanentError));
}

TEST(AuthState, IllegalTransitionsAreRejected) {
    EXPECT_FALSE(next_auth_state(AuthState::Unauthenticated, AuthEvent::SignedIn).has_value());
    EXPECT_FALSE(next_auth_state(AuthState::Authorized, AuthEvent::CodeRequested).has_value());
    EXPECT_FALSE(next_auth_state(AuthState::CodeRequired, AuthEvent::SignedIn).has_value());
    EXPECT_FALSE(next_auth_state(AuthState::Disconnected, AuthEvent::SignedIn).has_value());
}

TEST(AuthState, EveryStateHasAName) {
    for (auto s : kAllStates) {
        EXPECT_NE(std::string(to_string(s)), "unknown");
    }
}

TEST(AuthErrorMapping, KnownLoginErrors) {
    EXPECT_EQ(classify_auth_error(400, "PHONE_CODE_INVALID"), AuthEvent::InvalidCode);
    EXPECT_EQ(classify_auth_error(400, "PHONE_CODE_EMPTY"), AuthEvent::InvalidCode);
    EXPECT_EQ(classify_auth_error(400, "PHONE_CODE_EXPIRED"), AuthEvent::ExpiredCode);
    EXPECT_EQ(classify_auth_error(401, "SESSION_PASSWORD_NEEDED"), AuthEvent::PasswordNeeded);
    EXPECT_EQ(classify_auth_error(400, "PASSWORD_HASH_INVALID"), AuthEvent::InvalidPassword);
    EXPECT_EQ(classify_auth_error(401, "AUTH_KEY_UNREGISTERED"), AuthEvent::SessionRevoked);
    EXPECT_EQ(classify_auth_error(401, "SESSION_REVOKED"), AuthEvent::SessionRevoked);
    EXPECT_EQ(classify_auth_error(401, "SESSION_EXPIRED"), AuthEvent::SessionExpired);
    EXPECT_EQ(classify_auth_error(401, "USER_DEACTIVATED_BAN"), AuthEvent::PermanentFailure);
    EXPECT_EQ(classify_auth_error(400, "PHONE_NUMBER_INVALID"), AuthEvent::PermanentFailure);
}

TEST(AuthErrorMapping, FloodWaitAndServerErrorsAreTransient) {
    EXPECT_EQ(classify_auth_error(420, "FLOOD_WAIT_30"), AuthEvent::TransientFailure);
    EXPECT_EQ(classify_auth_error(420, "SLOWMODE_WAIT_10"), AuthEvent::TransientFailure);
    EXPECT_EQ(classify_auth_error(500, "INTERNAL"), AuthEvent::TransientFailure);
    EXPECT_EQ(classify_auth_error(503, "TIMEOUT"), AuthEvent::TransientFailure);
}

TEST(AuthErrorMapping, UnknownClientErrorsAreNotRetried) {
    EXPECT_EQ(classify_auth_error(400, "SOMETHING_NEW"), AuthEvent::PermanentFailure);
    EXPECT_EQ(classify_auth_error(0, ""), AuthEvent::PermanentFailure);
}

TEST(AuthErrorMapping, MappedEventsAreAcceptedInAuthenticating) {
    // Every login RPC runs in Authenticating, so each error event must be legal from there.
    for (auto [code, msg] : {std::pair{400, "PHONE_CODE_INVALID"}, {400, "PHONE_CODE_EXPIRED"},
                             {401, "SESSION_PASSWORD_NEEDED"}, {400, "PASSWORD_HASH_INVALID"},
                             {420, "FLOOD_WAIT_5"}, {400, "PHONE_NUMBER_INVALID"}}) {
        auto ev = classify_auth_error(code, msg);
        EXPECT_TRUE(next_auth_state(AuthState::Authenticating, ev).has_value()) << msg;
    }
}
