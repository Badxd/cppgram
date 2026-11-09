// payments.* calls against a scripted fake DC. Offline only. NOT RUN in this session.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::SavedStarGiftRef;
using cppgram::StarsAmountInfo;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;

namespace {

constexpr std::uint32_t kBoolTrue = 0x997275b5u;
constexpr std::uint32_t kUpdatesTooLong = 0xe317af7eu;
constexpr std::uint32_t kVectorId = 0x1cb5c415u;
constexpr std::uint32_t kCheckedGiftCodeId = 0xeb983f8fu;
constexpr std::uint32_t kExportedInvoiceId = 0xaed0cbd9u;

std::string session_path(const char* tag) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-pay-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
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

PeerRef user_peer() {
    PeerRef p;
    p.kind = PeerKind::User;
    p.id = 555;
    p.access_hash = 666;
    p.has_access_hash = true;
    return p;
}

}  // namespace

TEST(PaymentsStarsFakeDc, CancelStarsSubscriptionReadsBoolTrue) {
    FakeDc dc;
    std::uint32_t seen = 0;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::payments_botCancelStarsSubscription::kId) return false;
        seen = id;
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string path = session_path("cancel");
    Client client(options_for(dc, path));
    client.connect();
    EXPECT_TRUE(client.bot_cancel_stars_subscription(user_peer(), "charge-1", true));
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(seen, fn::payments_botCancelStarsSubscription::kId);
}

TEST(PaymentsStarsFakeDc, CraftStarGiftReturnsTrueOnUpdates) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::payments_craftStarGift::kId) return false;
        out.write_uint32(kUpdatesTooLong);
        return true;
    });
    const std::string path = session_path("craft");
    Client client(options_for(dc, path));
    client.connect();
    SavedStarGiftRef gift;
    gift.kind = SavedStarGiftRef::Kind::User;
    gift.msg_id = 12;
    EXPECT_TRUE(client.craft_star_gift({gift}));
    EXPECT_THROW(client.craft_star_gift({}), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
}

TEST(PaymentsStarsFakeDc, SendStarGiftOfferRejectsEmptySlugBeforeRpc) {
    FakeDc dc;
    bool rpc_seen = false;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter&) {
        if (id == fn::payments_sendStarGiftOffer::kId) rpc_seen = true;
        return false;
    });
    const std::string path = session_path("offer");
    Client client(options_for(dc, path));
    client.connect();
    StarsAmountInfo price;
    price.amount = 100;
    EXPECT_THROW(client.send_star_gift_offer(user_peer(), "", price, 3600), std::invalid_argument);
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_FALSE(rpc_seen);
}

TEST(PaymentsStarsFakeDc, CheckGiftCodeMapsDaysAndUnusedState) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::payments_checkGiftCode::kId) return false;
        out.write_uint32(kCheckedGiftCodeId);
        out.write_uint32(0);            // flags: no from, no to, unused
        out.write_int32(1800000000);    // date
        out.write_int32(30);            // days
        out.write_uint32(kVectorId);    // chats
        out.write_int32(0);
        out.write_uint32(kVectorId);    // users
        out.write_int32(0);
        return true;
    });
    const std::string path = session_path("gift");
    Client client(options_for(dc, path));
    client.connect();
    const auto info = client.check_gift_code("abc");
    client.disconnect();
    std::filesystem::remove(path);
    EXPECT_EQ(info.months_or_days, 30);
    EXPECT_EQ(info.date, 1800000000);
    EXPECT_FALSE(info.has_from);
    EXPECT_FALSE(info.has_to);
    EXPECT_EQ(info.used_date, 0);
}

TEST(PaymentsStarsFakeDc, ExportInvoiceReturnsUrl) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, cppgram::mtproto::TlReader&, cppgram::mtproto::TlWriter& out) {
        if (id != fn::payments_exportInvoice::kId) return false;
        out.write_uint32(kExportedInvoiceId);
        out.write_string("https://invoice.example/x");
        return true;
    });
    const std::string path = session_path("export");
    Client client(options_for(dc, path));
    client.connect();
    auto media = std::make_shared<cppgram::mtproto::tl::InputMedia>(
        cppgram::mtproto::tl::InputMedia{cppgram::mtproto::tl::types::inputMediaEmpty{}});
    EXPECT_EQ(client.export_invoice(media), "https://invoice.example/x");
    client.disconnect();
    std::filesystem::remove(path);
}
