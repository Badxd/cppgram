// Writes the valid seed corpora for fuzz_plain_unwrap, fuzz_decrypt_packet and fuzz_transport_framing.
// Usage: make_mtproto_seeds <output-dir>
// Writes valid seed inputs for the three harnesses. Run once; the output directories become the corpora.
#include <cstdio>
#include <filesystem>
#include <string>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/transport.h"

using namespace cppgram::mtproto;

static void write_file(const std::string& path, const Bytes& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string root = argv[1];
    for (const char* d : {"plain", "crypto", "transport"}) std::filesystem::create_directories(root + "/" + d);
    const Bytes key(256, 0x5a);
    // Plain: valid packets with exact live-like trailing counts, and a truncated one.
    for (std::size_t i = 0; i < 4; ++i) {
        Bytes packet = wrap_plain_message(Bytes(8 + 4 * i, 0x11), 0x1000 + i);
        write_file(root + "/plain/valid_" + std::to_string(i), packet);
        Bytes trailed = packet;
        trailed.insert(trailed.end(), (i == 0 ? 22u : i == 1 ? 102u : 123u), 0xAB);
        write_file(root + "/plain/trailing_" + std::to_string(i), trailed);
    }
    // Encrypted: valid packets in both directions, plus one with trailing bytes.
    InnerMessage msg{};
    msg.body = Bytes(40, 0x22);
    write_file(root + "/crypto/valid_client", concat(Bytes{1}, encrypt_packet(key, serialize_inner(msg), true)));  // direction byte 1 = outgoing
    write_file(root + "/crypto/valid_server", concat(Bytes{0}, encrypt_packet(key, serialize_inner(msg), false)));  // 0 = incoming
    Bytes trailed = concat(Bytes{0}, encrypt_packet(key, serialize_inner(msg), false));
    trailed.insert(trailed.end(), 102, 0xCD);
    write_file(root + "/crypto/valid_trailing102", trailed);
    // Transport: one frame of each kind, prefixed by the kind selector byte used by fuzz_transport.
    const std::uint8_t kinds_sel[] = {0, 1, 2};
    for (std::uint8_t k = 0; k < 3; ++k) {
        Transport t(k == 0 ? TransportKind::Abridged : k == 1 ? TransportKind::Intermediate : TransportKind::Full);
        Bytes frame = t.encode(Bytes(64, 0x33));
        Bytes seed{kinds_sel[k]};
        seed.insert(seed.end(), frame.begin(), frame.end());
        write_file(root + "/transport/frame_" + std::to_string(k), seed);
    }
    return 0;
}
