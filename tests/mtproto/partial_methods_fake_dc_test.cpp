// Regression tests for the CDN reupload retry, story media upload, and decoded result types, against scripted fake
// DCs. Offline only. NOT RUN in this session: the owner runs the suite.
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "fake_dc.h"

using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::MediaKind;
using cppgram::PeerKind;
using cppgram::testing::FakeDc;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;
using cppgram::mtproto::Bytes;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;

namespace {

constexpr std::uint32_t kBoolTrueId = 0x997275b5u;
constexpr std::uint32_t kVectorId = 0x1cb5c415u;

std::string temp_name(const char* tag, const char* ext) {
    static std::atomic<int> counter{0};
    return (std::filesystem::temp_directory_path() /
            ("cppgram-partial-" + std::string(tag) + "-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter++) + ext))
        .string();
}

ClientOptions options_for(FakeDc& dc, const std::string& session, std::vector<cppgram::mtproto::RsaPublicKey> keys) {
    ClientOptions o;
    o.api_id = 4242;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = std::move(keys);
    o.session_path = session;
    o.connect_timeout_ms = 2000;
    o.verify_cdn_hashes = true;
    return o;
}

Bytes payload_bytes() {
    Bytes b;
    for (int i = 0; i < 300; ++i) b.push_back(static_cast<std::uint8_t>('a' + (i % 26)));
    return b;
}

}  // namespace

// The CDN answers the first chunk with cdnFileReuploadNeeded. The client sends upload.reuploadCdnFile to the main DC,
// merges the returned hashes, and asks for the same chunk again. The retry passes the hash check and the plaintext
// matches the original payload.
TEST(PartialMethodsFakeDc, CdnReuploadRecoversAndVerifiesHashes) {
    FakeDc main_dc;
    FakeDc cdn_dc;
    const Bytes plain = payload_bytes();
    const Bytes key(32, 0x11);
    const Bytes iv(16, 0x22);
    const Bytes encrypted = cppgram::mtproto::aes_ctr_crypt(plain, key, cppgram::mtproto::cdn_counter_iv(iv, 0));
    const Bytes request_token{0xAB, 0xCD};

    int reupload_calls = 0;
    Bytes seen_request_token;
    bool hashes_fetched = false;
    main_dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::invokeWithLayer<fn::initConnection<fn::help_getConfig>>::kId) {
            ty::config cfg;
            ty::dcOption opt;
            opt.id = 2;
            opt.ip_address = "127.0.0.1";
            opt.port = cdn_dc.port();
            opt.cdn = true;
            cfg.dc_options.push_back(std::make_shared<tl::DcOption>(tl::DcOption{opt}));
            cfg.serialize(out);
            return true;
        }
        if (id == fn::upload_getFile::kId) {
            ty::upload_fileCdnRedirect redirect;
            redirect.dc_id = 2;
            redirect.file_token = Bytes{0x01, 0x02};
            redirect.encryption_key = key;
            redirect.encryption_iv = iv;
            redirect.serialize(out);  // no file_hashes: the hash check must fetch them from reupload
            return true;
        }
        if (id == fn::upload_reuploadCdnFile::kId) {
            ++reupload_calls;
            (void)args.read_bytes();  // file_token
            seen_request_token = args.read_bytes();
            ty::fileHash fh;
            fh.offset = 0;
            fh.limit = static_cast<std::int32_t>(encrypted.size());
            fh.hash = cppgram::mtproto::sha256(encrypted);
            out.write_uint32(kVectorId);
            out.write_int32(1);
            fh.serialize(out);
            return true;
        }
        if (id == fn::upload_getCdnFileHashes::kId) {
            hashes_fetched = true;
            return false;
        }
        return false;
    });

    int cdn_calls = 0;
    cdn_dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::upload_getCdnFile::kId) return false;
        ++cdn_calls;
        if (cdn_calls == 1) {
            ty::upload_cdnFileReuploadNeeded need;
            need.request_token = request_token;
            need.serialize(out);
        } else {
            ty::upload_cdnFile file;
            file.bytes = encrypted;
            file.serialize(out);
        }
        return true;
    });

    const std::string session = temp_name("cdn-session", ".session");
    const std::string dest = temp_name("cdn-file", ".bin");
    Client client(options_for(main_dc, session, {main_dc.public_key(), cdn_dc.public_key()}));
    client.connect();
    cppgram::Message msg;
    msg.media.kind = MediaKind::Document;
    msg.media.id = 77;
    msg.media.access_hash = 88;
    msg.media.file_reference = Bytes{1, 2, 3};
    msg.media.size = static_cast<std::int64_t>(plain.size());
    msg.media.dc_id = 2;
    const std::int64_t written = client.download_media(msg, dest);
    client.disconnect();

    std::ifstream in(dest, std::ios::binary);
    const Bytes got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(dest);
    std::filesystem::remove(session);

    EXPECT_EQ(reupload_calls, 1);
    EXPECT_EQ(seen_request_token, request_token) << "reupload must carry the request token from the CDN";
    EXPECT_EQ(cdn_calls, 2) << "the same chunk must be requested again after the reupload";
    EXPECT_FALSE(hashes_fetched) << "hashes from the reupload must be used without a separate hash fetch";
    EXPECT_EQ(written, static_cast<std::int64_t>(plain.size()));
    EXPECT_EQ(got, plain);
}

// If the CDN keeps asking for a reupload, the client stops after its per-chunk limit instead of looping forever.
TEST(PartialMethodsFakeDc, CdnReuploadGivesUpAfterLimit) {
    FakeDc main_dc;
    FakeDc cdn_dc;
    const Bytes plain = payload_bytes();
    const Bytes key(32, 0x11);
    const Bytes iv(16, 0x22);
    const Bytes encrypted = cppgram::mtproto::aes_ctr_crypt(plain, key, cppgram::mtproto::cdn_counter_iv(iv, 0));

    int reupload_calls = 0;
    main_dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id == fn::invokeWithLayer<fn::initConnection<fn::help_getConfig>>::kId) {
            ty::config cfg;
            ty::dcOption opt;
            opt.id = 2;
            opt.ip_address = "127.0.0.1";
            opt.port = cdn_dc.port();
            opt.cdn = true;
            cfg.dc_options.push_back(std::make_shared<tl::DcOption>(tl::DcOption{opt}));
            cfg.serialize(out);
            return true;
        }
        if (id == fn::upload_getFile::kId) {
            ty::upload_fileCdnRedirect redirect;
            redirect.dc_id = 2;
            redirect.file_token = Bytes{0x01, 0x02};
            redirect.encryption_key = key;
            redirect.encryption_iv = iv;
            redirect.serialize(out);
            return true;
        }
        if (id == fn::upload_reuploadCdnFile::kId) {
            ++reupload_calls;
            ty::fileHash fh;
            fh.offset = 0;
            fh.limit = static_cast<std::int32_t>(encrypted.size());
            fh.hash = cppgram::mtproto::sha256(encrypted);
            out.write_uint32(kVectorId);
            out.write_int32(1);
            fh.serialize(out);
            return true;
        }
        return false;
    });
    cdn_dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::upload_getCdnFile::kId) return false;
        ty::upload_cdnFileReuploadNeeded need;
        need.request_token = Bytes{0x0F};
        need.serialize(out);
        return true;
    });

    const std::string session = temp_name("cdn-limit-session", ".session");
    const std::string dest = temp_name("cdn-limit-file", ".bin");
    Client client(options_for(main_dc, session, {main_dc.public_key(), cdn_dc.public_key()}));
    client.connect();
    cppgram::Message msg;
    msg.media.kind = MediaKind::Document;
    msg.media.id = 77;
    msg.media.access_hash = 88;
    msg.media.file_reference = Bytes{1, 2, 3};
    msg.media.size = static_cast<std::int64_t>(plain.size());
    msg.media.dc_id = 2;
    EXPECT_ANY_THROW(client.download_media(msg, dest));
    client.disconnect();
    std::filesystem::remove(dest);
    std::filesystem::remove(session);

    // The client makes at most kMaxCdnReuploadsPerChunk (3) reupload requests for one chunk.
    EXPECT_LE(reupload_calls, 3);
    EXPECT_GE(reupload_calls, 1);
}

// Story photo edit: the file is uploaded with upload.saveFilePart and stories.editStory carries an
// inputMediaUploadedPhoto with flags.0 set.
TEST(PartialMethodsFakeDc, StoryEditUploadsPhotoAndSendsUploadedMedia) {
    FakeDc dc;
    int parts = 0;
    bool media_flag = false;
    bool photo_media = false;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            ++parts;
            out.write_uint32(kBoolTrueId);
            return true;
        }
        if (id == fn::stories_editStory::kId) {
            const std::uint32_t flags = args.read_uint32();
            (void)cppgram::mtproto::tl::InputPeer::read(args);
            (void)args.read_int32();  // story id
            media_flag = (flags & 1u) != 0;
            if (media_flag) {
                const cppgram::mtproto::tl::InputMedia media = cppgram::mtproto::tl::InputMedia::read(args);
                photo_media = std::holds_alternative<ty::inputMediaUploadedPhoto>(media.value);
            }
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });

    const std::string session = temp_name("story-photo-session", ".session");
    const std::string media = temp_name("story-photo", ".jpg");
    {
        std::ofstream f(media, std::ios::binary);
        f << "not really a jpeg";
    }
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    cppgram::StoryEditOptions options;
    options.media_path = media;
    options.media_as_photo = true;
    const cppgram::PeerRef chat{PeerKind::User, 555, 777, true};
    const bool ok = client.edit_story(chat, 9, options);
    client.disconnect();
    std::filesystem::remove(media);
    std::filesystem::remove(session);

    EXPECT_TRUE(ok);
    EXPECT_GE(parts, 1) << "the file must be uploaded before the story is edited";
    EXPECT_TRUE(media_flag) << "stories.editStory must set flags.0 when media is sent";
    EXPECT_TRUE(photo_media) << "a photo edit sends inputMediaUploadedPhoto";
}

// Story video edit: the file is sent as inputMediaUploadedDocument with a filename attribute and an mp4 mime type.
TEST(PartialMethodsFakeDc, StoryEditUploadsVideoAsDocumentWithFilename) {
    FakeDc dc;
    bool doc_media = false;
    std::string mime;
    bool has_filename = false;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id == fn::upload_saveFilePart::kId) {
            out.write_uint32(kBoolTrueId);
            return true;
        }
        if (id == fn::stories_editStory::kId) {
            const std::uint32_t flags = args.read_uint32();
            (void)cppgram::mtproto::tl::InputPeer::read(args);
            (void)args.read_int32();
            if ((flags & 1u) != 0) {
                const cppgram::mtproto::tl::InputMedia media = cppgram::mtproto::tl::InputMedia::read(args);
                if (const auto* doc = std::get_if<ty::inputMediaUploadedDocument>(&media.value)) {
                    doc_media = true;
                    mime = doc->mime_type;
                    for (const auto& a : doc->attributes) {
                        if (a && std::holds_alternative<ty::documentAttributeFilename>(a->value)) has_filename = true;
                    }
                }
            }
            ty::updatesTooLong done;
            done.serialize(out);
            return true;
        }
        return false;
    });

    const std::string session = temp_name("story-video-session", ".session");
    const std::string media = temp_name("story-video", ".mp4");
    {
        std::ofstream f(media, std::ios::binary);
        f << "not really a video";
    }
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    cppgram::StoryEditOptions options;
    options.media_path = media;
    options.media_as_photo = false;
    const cppgram::PeerRef chat{PeerKind::User, 555, 777, true};
    EXPECT_TRUE(client.edit_story(chat, 9, options));
    client.disconnect();
    std::filesystem::remove(media);
    std::filesystem::remove(session);

    EXPECT_TRUE(doc_media);
    EXPECT_EQ(mime, "video/mp4");
    EXPECT_TRUE(has_filename) << "video uploads carry documentAttributeFilename";
}

// messages.getPeerSettings decodes the boolean flags of the peer settings, not only a subset.
TEST(PartialMethodsFakeDc, PeerSettingsDecodesFlags) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getPeerSettings::kId) return false;
        ty::messages_peerSettings ps;
        ty::peerSettings s;
        s.report_spam = true;
        s.add_contact = true;
        s.business_bot_paused = true;
        s.geo_distance = 250;
        ps.settings = std::make_shared<tl::PeerSettings>(tl::PeerSettings{s});
        ps.serialize(out);
        return true;
    });

    const std::string session = temp_name("peer-settings-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    // A user peer needs its access hash (inputUser). Without one the client refuses before any RPC.
    const cppgram::PeerSettingsInfo info = client.get_peer_settings(cppgram::PeerRef{PeerKind::User, 555, 777, true});
    client.disconnect();
    std::filesystem::remove(session);

    EXPECT_TRUE(info.report_spam);
    EXPECT_TRUE(info.add_contact);
    EXPECT_TRUE(info.business_bot_paused);
    EXPECT_FALSE(info.block_contact);
    ASSERT_TRUE(info.geo_distance.has_value());
    EXPECT_EQ(*info.geo_distance, 250);
}

// Already joined shared folder: filter id, missing and already-present peers are decoded.
TEST(PartialMethodsFakeDc, FolderInviteAlreadyJoinedDecodesPeers) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::chatlists_checkChatlistInvite::kId) return false;
        ty::chatlists_chatlistInviteAlready already;
        already.filter_id = 7;
        already.missing_peers.push_back(std::make_shared<tl::Peer>(tl::Peer{ty::peerUser{1}}));
        already.already_peers.push_back(std::make_shared<tl::Peer>(tl::Peer{ty::peerUser{2}}));
        already.serialize(out);
        return true;
    });

    const std::string session = temp_name("invite-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    const cppgram::FolderInviteCheck check = client.check_folder_invite_link("https://t.me/addlist/abc?x=1");
    client.disconnect();
    std::filesystem::remove(session);

    EXPECT_TRUE(check.already_joined);
    EXPECT_EQ(check.filter_id, 7);
    ASSERT_EQ(check.missing_peers.size(), 1u);
    EXPECT_EQ(check.missing_peers[0].kind, PeerKind::User);
    EXPECT_EQ(check.missing_peers[0].id, 1);
    ASSERT_EQ(check.already_peers.size(), 1u);
    EXPECT_EQ(check.already_peers[0].id, 2);
    EXPECT_EQ(check.peer_count, 2);
}

// Top peers: the category and each peer's rating are decoded.
TEST(PartialMethodsFakeDc, TopPeersDecodesCategoryAndRating) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::contacts_getTopPeers::kId) return false;
        ty::contacts_topPeers tp;
        ty::topPeerCategoryPeers cat;
        cat.category = std::make_shared<tl::TopPeerCategory>(tl::TopPeerCategory{ty::topPeerCategoryCorrespondents{}});
        cat.count = 5;
        ty::topPeer top;
        top.peer = std::make_shared<tl::Peer>(tl::Peer{ty::peerUser{9}});
        top.rating = 1.5;
        cat.peers.push_back(std::make_shared<tl::TopPeer>(tl::TopPeer{top}));
        tp.categories.push_back(std::make_shared<tl::TopPeerCategoryPeers>(tl::TopPeerCategoryPeers{cat}));
        tp.serialize(out);
        return true;
    });

    const std::string session = temp_name("top-peers-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    const auto categories = client.get_top_peers(true);
    client.disconnect();
    std::filesystem::remove(session);

    ASSERT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories[0].category, cppgram::TopPeerCategory::Correspondents);
    EXPECT_EQ(categories[0].count, 5);
    ASSERT_EQ(categories[0].peers.size(), 1u);
    EXPECT_EQ(categories[0].peers[0].peer.id, 9);
    EXPECT_DOUBLE_EQ(categories[0].peers[0].rating, 1.5);
}

// Recent stickers: the sticker document, its emoji, the pack and the last-used dates are all decoded.
TEST(PartialMethodsFakeDc, RecentStickersDecodesDocumentsPacksAndDates) {
    FakeDc dc;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::messages_getRecentStickers::kId) return false;
        ty::messages_recentStickers rs;
        rs.hash = 3;
        ty::stickerPack pack;
        pack.emoticon = "x";
        pack.documents.push_back(500);
        rs.packs.push_back(std::make_shared<tl::StickerPack>(tl::StickerPack{pack}));
        ty::document doc;
        doc.id = 500;
        doc.access_hash = 600;
        doc.file_reference = Bytes{7};
        doc.date = 1;
        doc.mime_type = "image/webp";
        doc.size = 10;
        doc.dc_id = 2;
        ty::documentAttributeSticker attr;
        attr.alt = "smile";
        attr.stickerset = std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetEmpty{}});
        doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{attr}));
        rs.stickers.push_back(std::make_shared<tl::Document>(tl::Document{doc}));
        rs.dates.push_back(123);
        rs.serialize(out);
        return true;
    });

    const std::string session = temp_name("recent-stickers-session", ".session");
    Client client(options_for(dc, session, {dc.public_key()}));
    client.connect();
    const cppgram::StickerListInfo list = client.get_recent_stickers();
    client.disconnect();
    std::filesystem::remove(session);

    EXPECT_EQ(list.hash, 3);
    ASSERT_EQ(list.stickers.size(), 1u);
    EXPECT_EQ(list.stickers[0].media.id, 500);
    EXPECT_EQ(list.stickers[0].emoji, "smile");
    ASSERT_EQ(list.packs.size(), 1u);
    EXPECT_EQ(list.packs[0].emoticon, "x");
    ASSERT_EQ(list.dates.size(), 1u);
    EXPECT_EQ(list.dates[0], 123);
}
