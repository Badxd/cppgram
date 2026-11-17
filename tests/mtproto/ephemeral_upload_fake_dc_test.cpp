// Upload-then-send tests for ephemeral.sendMessage and ephemeral.editMessage with media_path, against a scripted fake
// DC. Offline only. NOT RUN in this session. The tests parse the request the client sends, so the media variant,
// filename attribute and file reference are checked, not only that a call returned.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::EphemeralMessageOptions;
using cppgram::PeerKind;
using cppgram::PeerRef;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

namespace {

constexpr std::uint32_t kSendMedia = 1u << 2;
constexpr std::uint32_t kSendPeer = 1u << 8;
constexpr std::uint32_t kEditMedia = 1u << 3;
constexpr std::uint32_t kEditPeer = 1u << 7;

std::string temp_dir_name(const char* tag) {
    static std::atomic<int> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cppgram-ephup-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
                std::to_string(counter++));
    std::filesystem::create_directories(dir);
    return dir.string();
}

ClientOptions options_for(FakeDc& dc, const std::string& session) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = session;
    o.connect_timeout_ms = 2000;
    o.verify_cdn_hashes = true;
    return o;
}

PeerRef user_peer(std::int64_t id) { return PeerRef{PeerKind::User, id, 99, true}; }

std::string write_file(const std::string& dir, const std::string& name, const std::string& body) {
    const auto path = (std::filesystem::path(dir) / name).string();
    std::ofstream out(path, std::ios::binary);
    out << body;
    return path;
}

// Files in dir other than the session file, which the client creates on its own.
std::size_t entry_count(const std::string& dir) {
    std::size_t n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().filename() != "s.session") ++n;
    }
    return n;
}

}  // namespace

// A local document is uploaded, then sent as inputMediaUploadedDocument with its file name and a MIME type. The
// progress callback reports the bytes and finishes at the total. No file is left behind in the directory.
TEST(EphemeralUploadFakeDc, DocumentPathIsUploadedThenSentWithFilename) {
    FakeDc dc;
    const std::string dir = temp_dir_name("doc");
    const std::string body(3000, 'x');
    const std::string path = write_file(dir, "note.txt", body);
    const std::size_t entries_before = entry_count(dir);  // the source file only

    int parts = 0;
    bool sent_uploaded_doc = false;
    std::string sent_name;
    std::string sent_mime;
    std::uint32_t flags = 0;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            ++parts;
            out.write_int32(0x997275b5u);  // boolTrue
            return true;
        }
        if (id != fn::ephemeral_sendMessage::kId) return false;
        flags = args.read_uint32();
        if (flags & kSendPeer) (void)cppgram::mtproto::tl::InputPeer::read(args);
        (void)cppgram::mtproto::tl::InputUser::read(args);
        (void)args.read_string();
        if (flags & kSendMedia) {
            const auto media = cppgram::mtproto::tl::InputMedia::read(args);
            if (const auto* doc = std::get_if<ty::inputMediaUploadedDocument>(&media.value)) {
                sent_uploaded_doc = true;
                sent_mime = doc->mime_type;
                for (const auto& a : doc->attributes) {
                    if (const auto* f = std::get_if<ty::documentAttributeFilename>(&a->value)) sent_name = f->file_name;
                }
            }
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = (std::filesystem::path(dir) / "s.session").string();
    Client client(options_for(dc, session));
    client.connect();
    EphemeralMessageOptions options;
    options.media_path = path;
    options.media_as_photo = false;
    std::int64_t last_done = -1;
    std::int64_t last_total = -1;
    int progress_calls = 0;
    options.upload_progress = [&](std::int64_t done, std::int64_t total) {
        ++progress_calls;
        last_done = done;
        last_total = total;
    };
    const bool ok = client.ephemeral_send_message(user_peer(777), "", options);
    client.disconnect();

    EXPECT_TRUE(ok);
    EXPECT_GE(parts, 1) << "the file must be uploaded before the send";
    EXPECT_TRUE(sent_uploaded_doc) << "the document must be sent as inputMediaUploadedDocument";
    EXPECT_EQ(sent_name, "note.txt");
    EXPECT_FALSE(sent_mime.empty());
    EXPECT_NE(flags & kSendMedia, 0u);
    EXPECT_GE(progress_calls, 1);
    EXPECT_EQ(last_total, static_cast<std::int64_t>(body.size()));
    EXPECT_EQ(last_done, last_total) << "progress must finish at the total";
    EXPECT_EQ(entry_count(dir), entries_before) << "the upload must not leave temporary files";
    std::filesystem::remove_all(dir);
}

// A local photo is uploaded and sent as inputMediaUploadedPhoto. The file reference is set.
TEST(EphemeralUploadFakeDc, PhotoPathIsSentAsUploadedPhoto) {
    FakeDc dc;
    const std::string dir = temp_dir_name("photo");
    const std::string path = write_file(dir, "pic.jpg", std::string(64, 'j'));
    bool sent_photo = false;
    bool had_file = false;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            out.write_int32(0x997275b5u);
            return true;
        }
        if (id != fn::ephemeral_sendMessage::kId) return false;
        const std::uint32_t flags = args.read_uint32();
        if (flags & kSendPeer) (void)cppgram::mtproto::tl::InputPeer::read(args);
        (void)cppgram::mtproto::tl::InputUser::read(args);
        (void)args.read_string();
        if (flags & kSendMedia) {
            const auto media = cppgram::mtproto::tl::InputMedia::read(args);
            if (const auto* photo = std::get_if<ty::inputMediaUploadedPhoto>(&media.value)) {
                sent_photo = true;
                had_file = static_cast<bool>(photo->file);
            }
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = (std::filesystem::path(dir) / "s.session").string();
    Client client(options_for(dc, session));
    client.connect();
    EphemeralMessageOptions options;
    options.media_path = path;  // media_as_photo defaults to true
    const bool ok = client.ephemeral_send_message(user_peer(777), "caption", options);
    client.disconnect();
    EXPECT_TRUE(ok);
    EXPECT_TRUE(sent_photo);
    EXPECT_TRUE(had_file);
    std::filesystem::remove_all(dir);
}

// Editing a message with a local file uploads it and sends the media with the edit (flag bit 3), without a message.
TEST(EphemeralUploadFakeDc, EditWithPathUploadsAndSendsMedia) {
    FakeDc dc;
    const std::string dir = temp_dir_name("edit");
    const std::string path = write_file(dir, "clip.bin", std::string(128, 'b'));
    bool edit_media = false;
    std::uint32_t flags = 0;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            out.write_int32(0x997275b5u);
            return true;
        }
        if (id != fn::ephemeral_editMessage::kId) return false;
        flags = args.read_uint32();
        if (flags & kEditPeer) (void)cppgram::mtproto::tl::InputPeer::read(args);
        (void)cppgram::mtproto::tl::InputUser::read(args);
        (void)args.read_int32();
        if (flags & kEditMedia) {
            const auto media = cppgram::mtproto::tl::InputMedia::read(args);
            edit_media = std::holds_alternative<ty::inputMediaUploadedDocument>(media.value);
        }
        ty::updatesTooLong done;
        done.serialize(out);
        return true;
    });

    const std::string session = (std::filesystem::path(dir) / "s.session").string();
    Client client(options_for(dc, session));
    client.connect();
    EphemeralMessageOptions options;
    options.media_path = path;
    options.media_as_photo = false;
    const bool ok = client.ephemeral_edit_message(user_peer(777), 5, "", options);
    client.disconnect();
    EXPECT_TRUE(ok);
    EXPECT_NE(flags & kEditMedia, 0u);
    EXPECT_TRUE(edit_media);
    EXPECT_EQ(flags & 1u, 0u) << "an empty text must not set the message flag";
    std::filesystem::remove_all(dir);
}

// Two media sources at once, an empty path, and a missing file all fail before an RPC is sent.
TEST(EphemeralUploadFakeDc, InvalidMediaSourcesFailBeforeAnyRpc) {
    FakeDc dc;
    const std::string dir = temp_dir_name("invalid");
    int uploads = 0;
    int sends = 0;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            ++uploads;
            out.write_int32(0x997275b5u);
            return true;
        }
        if (id == fn::ephemeral_sendMessage::kId) {
            ++sends;
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });

    const std::string session = (std::filesystem::path(dir) / "s.session").string();
    Client client(options_for(dc, session));
    client.connect();

    EphemeralMessageOptions two_sources;
    two_sources.media_path = (std::filesystem::path(dir) / "x").string();
    two_sources.uploaded_media = cppgram::UploadedFile{};
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "t", two_sources), std::invalid_argument);

    EphemeralMessageOptions empty_path;
    empty_path.media_path = std::string();
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "t", empty_path), std::invalid_argument);

    EphemeralMessageOptions missing;
    missing.media_path = (std::filesystem::path(dir) / "does-not-exist.bin").string();
    EXPECT_THROW(client.ephemeral_send_message(user_peer(777), "t", missing), cppgram::errors::FileTransferError);

    client.disconnect();
    EXPECT_EQ(uploads, 0) << "invalid options must not upload anything";
    EXPECT_EQ(sends, 0) << "invalid options must not send anything";
    std::filesystem::remove_all(dir);
}

// Cancelling an ephemeral send while its local file is uploading stops the upload at the next part boundary. The send
// is never issued, and the operation reports CancelledError. Runs the send through Client::async so the stop token is
// the one save_file checks. Offline only.
TEST(EphemeralUploadFakeDc, CancelDuringUploadStopsBeforeSend) {
    FakeDc dc;
    const std::string dir = temp_dir_name("cancel");
    std::atomic<int> parts{0};
    std::atomic<int> sends{0};
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            ++parts;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            out.write_int32(0x997275b5u);
            return true;
        }
        if (id == fn::ephemeral_sendMessage::kId) {
            ++sends;
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });

    const std::string path = write_file(dir, "big.bin", std::string(10 * 1024, 'x'));
    const std::string session = (std::filesystem::path(dir) / "s.session").string();
    ClientOptions options = options_for(dc, session);
    options.upload_part_size = 1024;
    Client client(options);
    client.connect();

    EphemeralMessageOptions send;
    send.media_path = path;
    auto result = client.async([peer = user_peer(777), send](Client& c, std::stop_token) {
        return c.ephemeral_send_message(peer, "t", send);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (parts.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_EQ(sends.load(), 0) << "a cancelled upload must not reach ephemeral.sendMessage";
    EXPECT_LT(parts.load(), 10) << "the upload must stop before the last part";
    client.disconnect();
    std::filesystem::remove_all(dir);
}
