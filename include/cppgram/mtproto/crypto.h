// cppgram - MTProto cryptography: hashes, AES-IGE, MTProto 2.0 framing, RSA, DH, pq factorization.
#pragma once

#include <cstdint>
#include <utility>

#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

class CryptoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct InnerMessage {
    std::uint64_t salt = 0;
    std::uint64_t session_id = 0;
    std::uint64_t message_id = 0;
    std::uint32_t seq_no = 0;
    Bytes body;
};

std::uint32_t crc32(const Bytes& data, std::uint32_t previous = 0);
Bytes sha1(const Bytes& data);
Bytes sha256(const Bytes& data);
Bytes md5(const Bytes& data);
Bytes random_bytes(std::size_t size);
Bytes slice(const Bytes& data, std::size_t offset, std::size_t size);
Bytes concat(const Bytes& a, const Bytes& b);

// AES-256 in IGE mode. `iv` is 32 bytes: first half is the previous ciphertext block, second half the previous plaintext block.
Bytes aes_ige_encrypt(const Bytes& data, const Bytes& key, const Bytes& iv);
Bytes aes_ige_decrypt(const Bytes& data, const Bytes& key, const Bytes& iv);
// AES-256-CTR (encryption and decryption are the same operation). iv is the 16-byte initial counter.
Bytes aes_ctr_crypt(const Bytes& data, const Bytes& key, const Bytes& iv);
// IV for a CDN chunk: the last 4 bytes of the file's encryption_iv are replaced by offset / 16 (big endian).
Bytes cdn_counter_iv(const Bytes& base_iv, std::int64_t offset);

// Builds the inner plaintext: salt, session_id, message_id, seq_no, body length, body.
Bytes serialize_inner(const InnerMessage& message);
InnerMessage parse_inner(const Bytes& plain);

// auth_key_id = low 64 bits of SHA1(auth_key), taken from bytes 12..20.
Bytes auth_key_id(const Bytes& auth_key);

// Full MTProto 2.0 encrypted packet: auth_key_id | msg_key | AES-IGE ciphertext.
// `outgoing` is true for client->server packets, false for server->client.
Bytes encrypt_packet(const Bytes& auth_key, const Bytes& inner_plain, bool outgoing);
// Returns the decrypted, still padded, inner plaintext. Use parse_inner() on the result.
Bytes decrypt_packet(const Bytes& auth_key, const Bytes& packet, bool outgoing);

// RSA with PKCS-less MTProto padding: SHA1(data) | data | random, to 255 bytes, then m^e mod n into 256 bytes.
Bytes rsa_encrypt_handshake(const Bytes& data, const Bytes& modulus_be, const Bytes& exponent_be);
// Telegram RSA key fingerprint: low 64 bits of SHA1(TL(n) | TL(e)), read little-endian.
std::uint64_t rsa_fingerprint(const Bytes& modulus_be, const Bytes& exponent_be);

// Diffie-Hellman modular exponentiation, result padded to the length of `modulus_be`.
Bytes dh_power(const Bytes& base_be, const Bytes& exponent_be, const Bytes& modulus_be);

// Factors pq (product of two primes below 2^32 each) into (p, q) with p < q.
std::pair<std::uint64_t, std::uint64_t> factorize_pq(std::uint64_t pq);

}  // namespace cppgram::mtproto
