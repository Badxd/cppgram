#include "cppgram/session_crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace cppgram::session_crypto {
namespace {

constexpr std::size_t kSaltLen = 16;
constexpr std::size_t kNonceLen = 12;
constexpr std::size_t kTagLen = 16;
constexpr std::size_t kKeyLen = 32;
constexpr std::size_t kHeaderLen = 4 + 3 + kSaltLen + kNonceLen;  // magic, params, salt, nonce
constexpr std::size_t kMinBlob = kHeaderLen + kTagLen;

// Default cost: N = 2^15, r = 8, p = 1 (about 32 MiB of scrypt memory). Accepted range bounds decrypt input.
constexpr std::uint8_t kLog2N = 15;
constexpr std::uint8_t kR = 8;
constexpr std::uint8_t kP = 1;
constexpr std::uint8_t kMinLog2N = 14;
constexpr std::uint8_t kMaxLog2N = 20;
constexpr std::uint8_t kMaxR = 16;
constexpr std::uint8_t kMaxP = 4;

[[noreturn]] void fail_auth() {
    throw std::runtime_error("session decryption failed: wrong passphrase or corrupted data");
}

void derive_key(std::string_view passphrase, const std::uint8_t* salt, std::uint8_t log2n, std::uint8_t r,
                std::uint8_t p, std::uint8_t* key) {
    const std::uint64_t n = std::uint64_t{1} << log2n;
    const std::uint64_t memory = 128ull * n * r;
    if (EVP_PBE_scrypt(passphrase.data(), passphrase.size(), salt, kSaltLen, n, r, p, memory * 2 + (1u << 20), key,
                       kKeyLen) != 1) {
        throw std::runtime_error("scrypt key derivation failed");
    }
}

// Secret material is wiped when it leaves scope, including on exceptions.
struct KeyGuard {
    std::uint8_t key[kKeyLen]{};
    ~KeyGuard() { OPENSSL_cleanse(key, sizeof key); }
};

}  // namespace

std::vector<std::uint8_t> encrypt(std::string_view passphrase, std::span<const std::uint8_t> plaintext) {
    if (passphrase.empty()) throw std::invalid_argument("session passphrase must not be empty");

    std::vector<std::uint8_t> out(kHeaderLen + kTagLen + plaintext.size());
    std::memcpy(out.data(), kMagic.data(), 4);
    out[4] = kLog2N;
    out[5] = kR;
    out[6] = kP;
    std::uint8_t* salt = out.data() + 7;
    std::uint8_t* nonce = salt + kSaltLen;
    std::uint8_t* tag = out.data() + kHeaderLen;
    std::uint8_t* body = tag + kTagLen;
    if (RAND_bytes(salt, kSaltLen) != 1 || RAND_bytes(nonce, kNonceLen) != 1) {
        throw std::runtime_error("secure random generation failed");
    }

    KeyGuard guard;
    derive_key(passphrase, salt, kLog2N, kR, kP, guard.key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) throw std::runtime_error("cannot create cipher context");
    int len = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceLen), nullptr) == 1 &&
              EVP_EncryptInit_ex(ctx, nullptr, nullptr, guard.key, nonce) == 1 &&
              EVP_EncryptUpdate(ctx, nullptr, &len, out.data(), static_cast<int>(kHeaderLen)) == 1;  // header as AAD
    if (ok && !plaintext.empty()) {
        ok = EVP_EncryptUpdate(ctx, body, &len, plaintext.data(), static_cast<int>(plaintext.size())) == 1;
    }
    if (ok) ok = EVP_EncryptFinal_ex(ctx, body + plaintext.size(), &len) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagLen), tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) throw std::runtime_error("session encryption failed");
    return out;
}

std::vector<std::uint8_t> decrypt(std::string_view passphrase, std::span<const std::uint8_t> blob) {
    if (passphrase.empty()) throw std::invalid_argument("session passphrase must not be empty");
    if (blob.size() < kMinBlob || !is_encrypted(blob)) {
        throw std::runtime_error("not an encrypted session blob");
    }
    const std::uint8_t log2n = blob[4];
    const std::uint8_t r = blob[5];
    const std::uint8_t p = blob[6];
    if (log2n < kMinLog2N || log2n > kMaxLog2N || r == 0 || r > kMaxR || p == 0 || p > kMaxP) {
        throw std::runtime_error("encrypted session has unsupported key-derivation parameters");
    }
    const std::uint8_t* salt = blob.data() + 7;
    const std::uint8_t* nonce = salt + kSaltLen;
    const std::uint8_t* tag = blob.data() + kHeaderLen;
    const std::uint8_t* body = tag + kTagLen;
    const std::size_t body_len = blob.size() - kMinBlob;

    KeyGuard guard;
    derive_key(passphrase, salt, log2n, r, p, guard.key);

    std::vector<std::uint8_t> out(body_len);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) throw std::runtime_error("cannot create cipher context");
    int len = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceLen), nullptr) == 1 &&
              EVP_DecryptInit_ex(ctx, nullptr, nullptr, guard.key, nonce) == 1 &&
              EVP_DecryptUpdate(ctx, nullptr, &len, blob.data(), static_cast<int>(kHeaderLen)) == 1;
    if (ok && body_len > 0) {
        ok = EVP_DecryptUpdate(ctx, out.data(), &len, body, static_cast<int>(body_len)) == 1;
    }
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagLen), const_cast<std::uint8_t*>(tag)) == 1;
    int final_len = 0;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, out.data() + body_len, &final_len) == 1;  // fails on a bad tag
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        OPENSSL_cleanse(out.data(), out.size());
        fail_auth();
    }
    return out;
}

std::optional<std::string> passphrase_from_env() {
    const char* value = std::getenv(std::string(kSessionPassphraseEnv).c_str());
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
}

bool is_encrypted(std::span<const std::uint8_t> blob) noexcept {
    return blob.size() >= 4 && std::memcmp(blob.data(), kMagic.data(), 4) == 0;
}

}  // namespace cppgram::session_crypto
