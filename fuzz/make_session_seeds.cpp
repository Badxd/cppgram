// Writes valid session strings as seed inputs for fuzz_session_string.
// Usage: make_session_seeds <output-dir>
// Keys are random 256-byte values (not real account keys). The seeds only exercise the decoder.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>
#include <vector>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/session_crypto.h"
#include "cppgram/session_string.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    std::filesystem::create_directories(argv[1]);
    for (int i = 0; i < 8; ++i) {
        cppgram::mtproto::SessionData data;
        data.dc_id = 1 + (i % 5);
        data.auth_key = cppgram::mtproto::random_bytes(256);
        data.server_salt = 0x0102030405060708ull * static_cast<std::uint64_t>(i + 1);
        data.time_offset = static_cast<std::int64_t>(i) - 4;
        const std::string text = cppgram::encode_session_string(data);
        const std::string path = std::string(argv[1]) + "/seed_" + std::to_string(i);
        std::ofstream out(path, std::ios::binary);
        out << text;
    }
    // Encrypted (CGE1) seeds for fuzz_session_crypto. The passphrase matches the one in that harness.
    // Plaintext is a session string; the blob is valid for decrypt() with the same passphrase.
    for (int i = 0; i < 4; ++i) {
        cppgram::mtproto::SessionData data;
        data.dc_id = 2;
        data.auth_key = cppgram::mtproto::random_bytes(256);
        data.server_salt = 0x1122334455667788ull + static_cast<std::uint64_t>(i);
        data.time_offset = 0;
        const std::string text = cppgram::encode_session_string(data);
        const std::vector<std::uint8_t> plain(text.begin(), text.end());
        const auto blob = cppgram::session_crypto::encrypt("fuzz-passphrase", plain);
        const std::string path = std::string(argv[1]) + "/cge1_seed_" + std::to_string(i);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }
    // Invalid CGE1 seeds: each one is a known-bad variant of a valid blob. They are kept so libFuzzer starts from
    // the boundaries (magic, parameters, truncation, tag, trailing data) instead of random bytes.
    {
        const std::string text = cppgram::encode_session_string(cppgram::mtproto::SessionData{});
        const std::vector<std::uint8_t> plain(text.begin(), text.end());
        const auto valid = cppgram::session_crypto::encrypt("fuzz-passphrase", plain);
        auto write = [&](const std::string& name, const std::vector<std::uint8_t>& bytes) {
            std::ofstream out(std::string(argv[1]) + "/" + name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        };
        auto mutated = [&](std::size_t index, std::uint8_t value) {
            auto copy = valid;
            copy[index] = value;
            return copy;
        };
        write("invalid_bad_magic", mutated(0, 'X'));
        write("invalid_log2n_too_small", mutated(4, 3));
        write("invalid_log2n_too_large", mutated(4, 40));
        write("invalid_r_zero", mutated(5, 0));
        write("invalid_p_zero", mutated(6, 0));
        write("invalid_truncated_header", std::vector<std::uint8_t>(valid.begin(), valid.begin() + 20));
        write("invalid_truncated_tag", std::vector<std::uint8_t>(valid.begin(), valid.begin() + 40));
        write("invalid_flipped_tag", mutated(40, valid[40] ^ 0x01));
        write("invalid_flipped_ciphertext", mutated(valid.size() - 1, valid.back() ^ 0x80));
        auto trailing = valid;
        trailing.push_back(0);
        write("invalid_trailing_byte", trailing);
        write("invalid_empty", {});
    }
    std::printf("wrote 8 session-string seeds, 4 valid CGE1 seeds and 11 invalid CGE1 seeds to %s\n", argv[1]);
    return 0;
}
