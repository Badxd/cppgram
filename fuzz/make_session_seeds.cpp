// Writes valid session strings as seed inputs for fuzz_session_string.
// Usage: make_session_seeds <output-dir>
// Keys are random 256-byte values (not real account keys). The seeds only exercise the decoder.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "cppgram/mtproto/crypto.h"
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
    std::printf("wrote 8 seeds to %s\n", argv[1]);
    return 0;
}
