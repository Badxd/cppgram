// Runs the server byte stream from frames_reference.py through CppGram's receive path and prints what came out.
//
// Path under test, per chunk (as Connection::receive_packet does it):
//   ObfuscatedClient::decrypt(chunk) -> Transport::feed -> Transport::next (one frame per call)
// and then, in "plain" mode, unwrap_plain_message(frame).
//
// Output per case:
//   case <name>
//   item <hex>        a frame (frames mode) or a plain body (plain mode)
//   item ERR          a transport or plain-packet error; the case stops at the first error
//   end
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/obfuscated.h"
#include "cppgram/mtproto/transport.h"

using cppgram::mtproto::Bytes;
using cppgram::mtproto::ObfuscatedClient;
using cppgram::mtproto::ObfuscatedFraming;
using cppgram::mtproto::Transport;
using cppgram::mtproto::TransportKind;

namespace {

Bytes from_hex(const std::string& hex) {
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::string to_hex(const Bytes& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

struct Case {
    std::string name, framing, mode, dc, secret, init, wire;
    std::vector<std::size_t> sizes;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cppgram_frames <frames_vectors.txt>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open vectors\n";
        return 2;
    }
    std::vector<Case> cases;
    Case cur;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key)) continue;
        if (key == "case") {
            cur = Case{};
            ss >> cur.name;
        } else if (key == "end") {
            cases.push_back(cur);
        } else if (key == "sizes") {
            std::size_t n;
            while (ss >> n) cur.sizes.push_back(n);
        } else {
            std::string value;
            ss >> value;
            if (key == "framing") cur.framing = value;
            else if (key == "mode") cur.mode = value;
            else if (key == "dc") cur.dc = value;
            else if (key == "secret") cur.secret = value;
            else if (key == "init") cur.init = value;
            else if (key == "wire") cur.wire = value;
        }
    }

    for (const Case& c : cases) {
        const Bytes secret_bytes = from_hex(c.secret);
        std::array<std::uint8_t, 16> secret{};
        std::copy(secret_bytes.begin(), secret_bytes.end(), secret.begin());
        const ObfuscatedFraming framing = c.framing == "padded" ? ObfuscatedFraming::PaddedIntermediate
                                                                : ObfuscatedFraming::Intermediate;
        ObfuscatedClient client = ObfuscatedClient::create(framing, static_cast<std::int16_t>(std::stoi(c.dc)),
                                                           secret, from_hex(c.init));
        const Bytes wire = from_hex(c.wire);

        std::cout << "case " << c.name << "\n";
        Transport transport(TransportKind::Intermediate);
        std::size_t pos = 0;
        bool stopped = false;
        auto emit_frame = [&](const Bytes& frame) {
            if (c.mode == "plain") {
                Bytes body;
                try {
                    body = cppgram::mtproto::unwrap_plain_message(frame);
                } catch (const std::exception&) {
                    std::cout << "item ERR\n";
                    stopped = true;
                    return;
                }
                std::cout << "item " << to_hex(body) << "\n";
            } else {
                std::cout << "item " << to_hex(frame) << "\n";
            }
        };
        // Chunk boundaries follow the vector's `sizes`; whatever remains is one final read.
        std::vector<std::size_t> reads = c.sizes;
        std::size_t consumed = 0;
        for (std::size_t r = 0; r <= reads.size() && !stopped; ++r) {
            std::size_t take = (r < reads.size()) ? reads[r] : wire.size() - consumed;
            if (consumed + take > wire.size()) take = wire.size() - consumed;
            if (take == 0) continue;
            Bytes chunk(wire.begin() + static_cast<std::ptrdiff_t>(consumed),
                        wire.begin() + static_cast<std::ptrdiff_t>(consumed + take));
            consumed += take;
            transport.feed(client.decrypt(chunk));
            try {
                while (!stopped) {
                    auto frame = transport.next();
                    if (!frame) break;
                    emit_frame(*frame);
                }
            } catch (const std::exception&) {
                std::cout << "item ERR\n";
                stopped = true;
            }
        }
        (void)pos;
        std::cout << "end\n";
    }
    return 0;
}
