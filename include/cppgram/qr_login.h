// QR login and authorization transfer (auth.exportLoginToken, importLoginToken, exportAuthorization,
// importAuthorization). Layer 233. Offline helpers are declared here; the Client methods that use them are in client.h.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppgram::mtproto::tl {
struct auth_LoginToken;
}

namespace cppgram {

enum class LoginTokenState {
    Token,     // not scanned yet: show qr_login_url(token) as a QR code and poll again when it expires
    MigrateTo, // the account lives on another data center: connect to dc_id and call export_login_token there
    Success    // the token was accepted; authorized_user_id is set
};

struct LoginTokenInfo {
    LoginTokenState state = LoginTokenState::Token;
    std::vector<std::uint8_t> token;          // empty for Success
    std::int32_t expires = 0;                 // unix time, only for Token
    std::int32_t dc_id = 0;                   // only for MigrateTo
    std::optional<std::int64_t> authorized_user_id; // only for Success
};

struct AuthorizationExport {
    std::int64_t id = 0;
    std::vector<std::uint8_t> bytes;
};

// RFC 4648 section 5 base64url, without padding.
std::string base64url_encode(const std::vector<std::uint8_t>& data);

// Converts the TL auth.LoginToken returned by the server. Throws std::runtime_error for sign-up or unknown variants.
LoginTokenInfo login_token_from_tl(const mtproto::tl::auth_LoginToken& value);

// "tg://login?token=" followed by the base64url token. Encode this string as a QR code.
std::string qr_login_url(const std::vector<std::uint8_t>& token);

} // namespace cppgram
