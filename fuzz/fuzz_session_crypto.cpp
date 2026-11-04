// libFuzzer target: encrypted session blob (CGE1) parsing, key derivation and authenticated decryption.
//
// Every input goes through three checks:
//   1. session_crypto::is_encrypted and the header validator, on any bytes.
//   2. The authenticated-open path (AES-256-GCM, header as AAD) with a key for the input's own salt and parameters.
//      Keys are cached per (salt, log2N, r, p) so most inputs skip scrypt. Mutated salts still pay for scrypt.
//   3. The full public decrypt() on a sample of inputs, so the production entry point stays exercised.
// Any std::exception is the expected result for malformed input. Crashes, hangs, sanitizer reports and
// allocations beyond the documented scrypt bound are the failures this target looks for.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <span>
#include <string_view>
#include <vector>

#include "cppgram/session_crypto.h"
#include "session_crypto_detail.h"

namespace {

constexpr std::string_view kPassphrase = "fuzz-passphrase";

struct KeyCache {
    bool valid = false;
    std::uint8_t salt[16]{};
    std::uint8_t params[3]{};
    std::vector<std::uint8_t> key;
};

KeyCache& cache() {
    static KeyCache c;
    return c;
}

// Returns the cached key when the blob's salt and parameters match, otherwise derives and caches a new one.
const std::vector<std::uint8_t>* key_for(std::span<const std::uint8_t> blob) {
    KeyCache& c = cache();
    if (c.valid && std::memcmp(blob.data() + 4, c.params, 3) == 0 && std::memcmp(blob.data() + 7, c.salt, 16) == 0) {
        return &c.key;
    }
    c.valid = false;
    c.key = cppgram::session_crypto::detail::derive_key_for(kPassphrase, blob);
    std::memcpy(c.params, blob.data() + 4, 3);
    std::memcpy(c.salt, blob.data() + 7, 16);
    c.valid = true;
    return &c.key;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> blob(data, size);
    try {
        (void)cppgram::session_crypto::is_encrypted(blob);
        cppgram::session_crypto::detail::validate_header(blob);
        const auto* key = key_for(blob);
        (void)cppgram::session_crypto::detail::open_with_key(key->data(), blob);
    } catch (const std::exception&) {
        // Malformed, truncated or tampered input is expected to throw.
    }
    // Sampled full path: includes the passphrase-to-key derivation inside decrypt().
    if (size % 64 == 0) {
        try {
            (void)cppgram::session_crypto::decrypt(kPassphrase, blob);
        } catch (const std::exception&) {
        }
    }
    return 0;
}
