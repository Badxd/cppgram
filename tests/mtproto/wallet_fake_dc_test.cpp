// TON wallet calls against a scripted fake DC. Offline only. NOT RUN in this session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::WalletReplacement;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kProofChallengeId = 0x99e41707u;
constexpr std::uint32_t kWalletStateEmptyId = 0x9cb9b2ecu;

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-wallet-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter++) + ".session"))
        .string();
}

ClientOptions options_for(FakeDc& dc, const std::string& path) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = path;
    o.connect_timeout_ms = 2000;
    return o;
}

}  // namespace

TEST(WalletFakeDc, ProofChallengeIsParsed) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::wallet_getProofChallenge::kId) return false;
        out.write_uint32(kProofChallengeId);
        out.write_string("payload-123");
        out.write_int32(1800000000);
        out.write_string("example.org");
        return true;
    });
    const std::string path = session_path("proof");
    Client client(options_for(dc, path));
    client.connect();
    const auto c = client.get_wallet_proof_challenge();
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(c.payload, "payload-123");
    EXPECT_EQ(c.expires, 1800000000);
    EXPECT_EQ(c.domain, "example.org");
}

TEST(WalletFakeDc, ReplaceWithNewWalletReadsEmptyState) {
    FakeDc dc;
    std::uint32_t wallet_ctor = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader& args, cppgram::mtproto::TlWriter& out) {
        if (id != fn::wallet_replaceWallet::kId) return false;
        args.read_uint32();                 // request flags
        wallet_ctor = args.read_uint32();   // inputWalletNew
        out.write_uint32(kWalletStateEmptyId);
        out.write_uint32(0);                // flags: not creating
        return true;
    });
    const std::string path = session_path("replace");
    Client client(options_for(dc, path));
    client.connect();
    WalletReplacement r;
    r.kind = WalletReplacement::Kind::New;
    const auto state = client.replace_wallet(r);
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(wallet_ctor, 0x63a440dcu);  // inputWalletNew
    EXPECT_TRUE(state.empty);
    EXPECT_FALSE(state.creating);
}
