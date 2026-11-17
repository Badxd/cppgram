// Dumps CppGram's obfuscated-transport output for the vectors written by reference.py.
// Reads the vector file from argv[1], prints one block per case:
//   case <name>
//   handshake <hex>     the 64 wire bytes
//   tx_cipher <hex>     encrypt(tx_plain)
//   rx_cipher <hex>     decrypt(rx_cipher_input) where the input is rx_plain (CTR is symmetric)
//   end
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cppgram/mtproto/obfuscated.h"

using cppgram::mtproto::Bytes;
using cppgram::mtproto::ObfuscatedClient;
using cppgram::mtproto::ObfuscatedFraming;

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
    std::string name;
    std::string framing;
    std::string dc;
    std::string secret;
    std::string init;
    std::string tx_plain;
    std::string rx_plain;
};

ObfuscatedFraming framing_of(const std::string& s) {
    if (s == "intermediate") return ObfuscatedFraming::Intermediate;
    if (s == "padded") return ObfuscatedFraming::PaddedIntermediate;
    if (s == "abridged") return ObfuscatedFraming::Abridged;
    throw std::runtime_error("unknown framing " + s);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cppgram_dump <vectors.txt>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    std::vector<Case> cases;
    Case cur;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key, value;
        if (!(ss >> key)) continue;
        if (key == "case") {
            cur = Case{};
            ss >> cur.name;
        } else if (key == "end") {
            cases.push_back(cur);
        } else {
            ss >> value;
            if (key == "framing") cur.framing = value;
            else if (key == "dc") cur.dc = value;
            else if (key == "secret") cur.secret = value;
            else if (key == "init") cur.init = value;
            else if (key == "tx_plain") cur.tx_plain = value;
            else if (key == "rx_plain") cur.rx_plain = value;
        }
    }

    for (const Case& c : cases) {
        std::array<std::uint8_t, 16> secret{};
        const Bytes secret_bytes = from_hex(c.secret);
        if (secret_bytes.size() != 16) {
            std::cerr << "secret must be 16 bytes\n";
            return 2;
        }
        std::copy(secret_bytes.begin(), secret_bytes.end(), secret.begin());

        ObfuscatedClient client = ObfuscatedClient::create(framing_of(c.framing),
                                                           static_cast<std::int16_t>(std::stoi(c.dc)), secret,
                                                           from_hex(c.init));
        const Bytes tx_cipher = client.encrypt(from_hex(c.tx_plain));
        const Bytes rx_cipher = client.decrypt(from_hex(c.rx_plain));

        std::cout << "case " << c.name << "\n";
        std::cout << "handshake " << to_hex(client.handshake()) << "\n";
        std::cout << "tx_cipher " << to_hex(tx_cipher) << "\n";
        std::cout << "rx_cipher " << to_hex(rx_cipher) << "\n";
        std::cout << "end\n";
    }
    return 0;
}
