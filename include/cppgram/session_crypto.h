// cppgram - passphrase-encrypted session blobs.
// Format "CGE1": magic(4) | log2N(1) | r(1) | p(1) | salt(16) | nonce(12) | tag(16) | ciphertext.
// Key: scrypt(passphrase, salt, N, r, p) -> 32 bytes. Cipher: AES-256-GCM with a fresh random nonce per encryption.
// The header is authenticated as additional data, so changing any header byte fails decryption.
// The passphrase comes from the environment variable named by kSessionPassphraseEnv, never from a file.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cppgram::session_crypto {

inline constexpr std::string_view kSessionPassphraseEnv = "CPPGRAM_SESSION_PASSPHRASE";
inline constexpr std::string_view kMagic = "CGE1";

// Encrypts plaintext. Throws std::invalid_argument on an empty passphrase.
std::vector<std::uint8_t> encrypt(std::string_view passphrase, std::span<const std::uint8_t> plaintext);

// Decrypts a blob. Throws std::runtime_error for a wrong passphrase, any tampering, truncation, or scrypt
// parameters outside the accepted range. All authentication failures give the same message.
std::vector<std::uint8_t> decrypt(std::string_view passphrase, std::span<const std::uint8_t> blob);

// True when the blob starts with the encrypted-session magic. Used to tell encrypted from plain session files.
bool is_encrypted(std::span<const std::uint8_t> blob) noexcept;

}  // namespace cppgram::session_crypto
