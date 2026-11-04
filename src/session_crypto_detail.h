// Internal seam for session_crypto. Not part of the public API.
// The fuzz harness uses these to reach the header and authenticated-decryption code without running scrypt on
// every input. decrypt() calls the same functions, so no validation is skipped in production.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cppgram::session_crypto::detail {

// Derives the 32-byte key for a blob's header parameters and salt (scrypt). Validates the header first.
std::vector<std::uint8_t> derive_key_for(std::string_view passphrase, std::span<const std::uint8_t> blob);

// Throws std::runtime_error for a blob with a bad size, magic or key-derivation parameters.
void validate_header(std::span<const std::uint8_t> blob);

// Authenticates and decrypts with a 32-byte key. Validates the header first. Throws std::runtime_error on any
// authentication failure. `key` must point to 32 bytes.
std::vector<std::uint8_t> open_with_key(const std::uint8_t* key, std::span<const std::uint8_t> blob);

}  // namespace cppgram::session_crypto::detail
