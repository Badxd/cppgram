// Media transfers (upload and download) against the loopback fake DC. Each test scripts the server's answers through
// FakeDc::set_handler. Offline only: no Telegram servers and no credentials are used. The api_hash is a dummy value.
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../mtproto/fake_dc.h"
#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/metrics.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/types.h"

using namespace std::chrono_literals;
using cppgram::Client;
using cppgram::ClientOptions;
using cppgram::testing::FakeDc;
using cppgram::mtproto::TlReader;
using cppgram::mtproto::TlWriter;
using cppgram::mtproto::Bytes;
namespace fn = cppgram::mtproto::tl::functions;
namespace ty = cppgram::mtproto::tl::types;
namespace tl = cppgram::mtproto::tl;

namespace {

constexpr std::uint32_t kBoolTrue = 0x997275b5u;
constexpr std::uint32_t kBoolFalse = 0xbc799737u;
constexpr std::uint32_t kDocumentLocation = 0xbad07584u;

std::string temp_dir_path(const std::string& name) {
    static std::atomic<int> counter{0};
    const auto dir = std::filesystem::temp_directory_path() /
                     ("cppgram-media-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

void write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string pattern(std::size_t size, unsigned seed) {
    std::string out(size, '\0');
    for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<char>((i * 31 + seed) & 0xff);
    return out;
}

ClientOptions options_for(FakeDc& dc, const std::string& session_path) {
    ClientOptions o;
    o.api_id = 1;
    o.api_hash = "00000000000000000000000000000000";  // dummy test value
    o.dc_ip = "127.0.0.1";
    o.dc_port = dc.port();
    o.dc_id = 2;
    o.server_keys = {dc.public_key()};
    o.session_path = session_path;
    o.keepalive_ms = 150;
    o.connect_timeout_ms = 2000;
    o.debug = std::getenv("CPPGRAM_TEST_DEBUG") != nullptr;
    o.retry.max_attempts = 3;
    o.retry.base = std::chrono::milliseconds(5);
    o.retry.cap = std::chrono::milliseconds(20);
    o.retry.max_flood_wait = std::chrono::seconds(60);
    return o;
}

// Shared record of what the fake server was asked to do.
struct Record {
    std::mutex mutex;
    std::vector<std::pair<std::int32_t, std::size_t>> parts;  // file_part, bytes
    std::vector<std::int32_t> part_totals;
    std::vector<bool> big_parts;
    std::vector<std::int64_t> get_offsets;
    std::atomic<int> part_calls{0};
};

// Writes a TL error (rpc_error) as the answer.
void answer_error(TlWriter& out, std::int32_t code, const std::string& message) {
    ty::rpc_error err;
    err.error_code = code;
    err.error_message = message;
    err.serialize(out);
}

// Reads a part of the upload.saveFilePart family and records it. Returns the part index.
std::int32_t record_part(std::uint32_t id, TlReader& args, Record& rec, std::size_t& size) {
    args.read_int64();  // file_id
    const std::int32_t part = args.read_int32();
    std::int32_t total = 0;
    if (id == fn::upload_saveBigFilePart::kId) total = args.read_int32();
    const Bytes bytes = args.read_bytes();
    size = bytes.size();
    std::lock_guard<std::mutex> lock(rec.mutex);
    rec.parts.emplace_back(part, bytes.size());
    rec.part_totals.push_back(total);
    rec.big_parts.push_back(id == fn::upload_saveBigFilePart::kId);
    return part;
}

// Serves upload.getFile for one document from `content`. Returns the argument offset through `offset_out`.
void serve_document_part(TlReader& args, const std::string& content, std::int64_t& offset_out, std::int32_t& limit_out,
                         TlWriter& out) {
    args.read_uint32();  // flags (precise and cdn_supported carry no value)
    const std::uint32_t location = args.read_uint32();
    if (location != kDocumentLocation) throw std::runtime_error("fake DC: only document locations are served");
    args.read_int64();      // id
    args.read_int64();      // access_hash
    args.read_bytes();      // file_reference
    args.read_string();     // thumb_size
    offset_out = args.read_int64();
    limit_out = args.read_int32();
    const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(offset_out), content.size());
    const std::size_t end = std::min<std::size_t>(begin + static_cast<std::size_t>(limit_out), content.size());
    ty::upload_file file;
    file.type = std::make_shared<tl::storage_FileType>(tl::storage_FileType{ty::storage_fileUnknown{}});
    file.mtime = 0;
    file.bytes.assign(content.begin() + static_cast<std::ptrdiff_t>(begin),
                      content.begin() + static_cast<std::ptrdiff_t>(end));
    tl::upload_File wrapped{file};
    wrapped.serialize(out);
}

cppgram::Message document_message(std::int64_t id, const std::string& content) {
    cppgram::Message msg;
    msg.media.kind = cppgram::MediaKind::Document;
    msg.media.id = id;
    msg.media.access_hash = 7;
    msg.media.file_reference = {1, 2, 3};
    msg.media.size = static_cast<std::int64_t>(content.size());
    return msg;
}

double counter_value(const std::vector<cppgram::metrics::FamilySnapshot>& snap, const std::string& name) {
    for (const auto& family : snap) {
        if (family.name == name && !family.series.empty()) return family.series.front().value;
    }
    return 0.0;
}

// ---- uploads

TEST(MediaUpload, SmallFileIsOnePartWithItsMd5AndTheCountIsMetered) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        record_part(id, args, rec, size);
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string path = temp_dir_path("abc.bin");
    write_file(path, "abc");
    auto registry = std::make_shared<cppgram::metrics::Registry>();
    auto options = options_for(dc, temp_dir_path("small.session"));
    options.metrics = registry;
    Client client(options);
    client.connect();

    const auto uploaded = client.save_file(path);
    ASSERT_EQ(rec.parts.size(), 1u);
    EXPECT_EQ(rec.parts[0].first, 0);
    EXPECT_EQ(rec.parts[0].second, 3u);
    ASSERT_NE(uploaded.handle, nullptr);
    const auto* input = std::get_if<ty::inputFile>(&uploaded.handle->value);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->md5_checksum, "900150983cd24fb0d6963f7d28e17f72") << "MD5 of 'abc'";
    EXPECT_EQ(input->parts, 1);
    EXPECT_EQ(uploaded.size, 3);
    EXPECT_DOUBLE_EQ(counter_value(registry->snapshot(), "cppgram_media_upload_bytes_total"), 3.0);
}

TEST(MediaUpload, LargeFileUsesBigPartsInOrderAndCoversTheWholeFile) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveBigFilePart::kId) return false;
        std::size_t size = 0;
        record_part(id, args, rec, size);
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string content = pattern(11 * 1024 * 1024, 3);
    const std::string path = temp_dir_path("big.bin");
    write_file(path, content);
    Client client(options_for(dc, temp_dir_path("big.session")));
    client.connect();

    const auto uploaded = client.save_file(path);
    ASSERT_EQ(rec.parts.size(), 22u) << "11 MiB at 512 KiB per part";
    std::size_t total_bytes = 0;
    for (std::size_t i = 0; i < rec.parts.size(); ++i) {
        EXPECT_EQ(rec.parts[i].first, static_cast<std::int32_t>(i)) << "parts must arrive in order";
        EXPECT_EQ(rec.part_totals[i], 22);
        EXPECT_TRUE(rec.big_parts[i]);
        total_bytes += rec.parts[i].second;
    }
    EXPECT_EQ(total_bytes, content.size());
    const auto* big = std::get_if<ty::inputFileBig>(&uploaded.handle->value);
    ASSERT_NE(big, nullptr);
    EXPECT_EQ(big->parts, 22);
}

TEST(MediaUpload, AServerErrorOnOnePartIsRetriedWithTheSameBytes) {
    FakeDc dc;
    Record rec;
    std::atomic<int> failures_left{1};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        const std::int32_t part = record_part(id, args, rec, size);
        if (part == 1 && failures_left.fetch_sub(1) > 0) {
            answer_error(out, 500, "INTERNAL");
        } else {
            out.write_uint32(kBoolTrue);
        }
        return true;
    });
    const std::string content = pattern(3 * 1024, 9);
    const std::string path = temp_dir_path("retry.bin");
    write_file(path, content);
    auto registry = std::make_shared<cppgram::metrics::Registry>();
    auto options = options_for(dc, temp_dir_path("retry.session"));
    options.upload_part_size = 1024;
    options.metrics = registry;
    Client client(options);
    client.connect();

    EXPECT_NO_THROW(client.save_file(path));
    int part1 = 0;
    for (const auto& [index, bytes] : rec.parts) {
        if (index == 1) {
            ++part1;
            EXPECT_EQ(bytes, 1024u) << "the resend carries the same bytes";
        }
    }
    EXPECT_EQ(part1, 2) << "one failed attempt, one success";
    EXPECT_DOUBLE_EQ(counter_value(registry->snapshot(), "cppgram_media_part_retries_total"), 1.0);
}

TEST(MediaUpload, ServerRefusingAPartIsAFileTransferError) {
    FakeDc dc;
    dc.set_handler([](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        args.read_int64();
        args.read_int32();
        out.write_uint32(kBoolFalse);
        return true;
    });
    const std::string path = temp_dir_path("refused.bin");
    write_file(path, pattern(2048, 1));
    auto options = options_for(dc, temp_dir_path("refused.session"));
    options.upload_part_size = 1024;
    Client client(options);
    client.connect();
    EXPECT_THROW(client.save_file(path), cppgram::errors::FileTransferError);
}

TEST(MediaUpload, ProgressIsReportedAfterEachPart) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        record_part(id, args, rec, size);
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string path = temp_dir_path("progress.bin");
    write_file(path, pattern(3 * 1024, 2));
    auto options = options_for(dc, temp_dir_path("progress.session"));
    options.upload_part_size = 1024;
    Client client(options);
    client.connect();

    std::vector<std::int64_t> seen;
    client.save_file(path, [&](std::int64_t done, std::int64_t total) {
        EXPECT_EQ(total, 3 * 1024);
        seen.push_back(done);
    });
    EXPECT_EQ(seen, (std::vector<std::int64_t>{1024, 2048, 3072}));
}

TEST(MediaUpload, CancellationStopsBetweenParts) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        record_part(id, args, rec, size);
        std::this_thread::sleep_for(100ms);
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string path = temp_dir_path("cancel.bin");
    write_file(path, pattern(10 * 1024, 4));
    auto options = options_for(dc, temp_dir_path("cancel.session"));
    options.upload_part_size = 1024;
    Client client(options);
    client.connect();

    auto result = client.save_file_async(path);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (rec.parts.empty() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    std::lock_guard<std::mutex> lock(rec.mutex);
    EXPECT_LT(rec.parts.size(), 10u) << "the upload must stop before the last part";
}

TEST(MediaUpload, EmptyFileIsRejectedBeforeAnyRequest) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader&, TlWriter&) {
        if (id == fn::upload_saveFilePart::kId) ++rec.part_calls;
        return false;
    });
    const std::string path = temp_dir_path("empty.bin");
    write_file(path, "");
    Client client(options_for(dc, temp_dir_path("empty.session")));
    client.connect();
    EXPECT_THROW(client.save_file(path), cppgram::errors::FileTransferError);
    EXPECT_EQ(rec.part_calls.load(), 0);
}

TEST(MediaUpload, TwoAsyncUploadsOnOneClientBothComplete) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        record_part(id, args, rec, size);
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string a = temp_dir_path("a.bin");
    const std::string b = temp_dir_path("b.bin");
    write_file(a, pattern(2048, 5));
    write_file(b, pattern(4096, 6));
    auto options = options_for(dc, temp_dir_path("two.session"));
    options.upload_part_size = 1024;
    Client client(options);
    client.connect();

    auto first = client.save_file_async(a);
    auto second = client.save_file_async(b);
    EXPECT_EQ(first.get().size, 2048);
    EXPECT_EQ(second.get().size, 4096);
    EXPECT_EQ(rec.parts.size(), 6u);
}

// ---- downloads

TEST(MediaDownload, SmallFileMatchesTheServerContent) {
    FakeDc dc;
    const std::string content = pattern(300 * 1024, 11);
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-small.session")));
    client.connect();
    const std::string dest = temp_dir_path("small.out");
    std::int64_t last_done = 0;
    const auto bytes = client.download_media(document_message(100, content), dest,
                                             [&](std::int64_t done, std::int64_t) { last_done = done; });
    EXPECT_EQ(bytes, static_cast<std::int64_t>(content.size()));
    EXPECT_EQ(last_done, static_cast<std::int64_t>(content.size()));
    EXPECT_EQ(read_file(dest), content);
    EXPECT_FALSE(std::filesystem::exists(dest + ".part"));
    EXPECT_FALSE(std::filesystem::exists(dest + ".part.meta"));
}

TEST(MediaDownload, LargeFileIsFetchedInOneMiBChunks) {
    FakeDc dc;
    Record rec;
    const std::string content = pattern(2 * 1024 * 1024 + 512 * 1024, 12);
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        std::lock_guard<std::mutex> lock(rec.mutex);
        rec.get_offsets.push_back(offset);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-large.session")));
    client.connect();
    const std::string dest = temp_dir_path("large.out");
    client.download_media(document_message(101, content), dest);
    EXPECT_EQ(rec.get_offsets, (std::vector<std::int64_t>{0, 1 << 20, 2 << 20}));
    EXPECT_EQ(read_file(dest), content);
}

TEST(MediaDownload, InterruptedDownloadResumesFromTheLastCompleteChunk) {
    FakeDc dc;
    Record rec;
    const std::string content = pattern(3 * 1024 * 1024, 13);
    std::atomic<bool> fail_once{true};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        // Serve the chunk into a scratch writer first, so the offset is known before deciding to fail it.
        std::int64_t served_offset = 0;
        std::int32_t served_limit = 0;
        TlWriter scratch;
        serve_document_part(args, content, served_offset, served_limit, scratch);
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            rec.get_offsets.push_back(served_offset);
        }
        if (served_offset == (1 << 20) * 2 && fail_once.exchange(false)) {
            answer_error(out, 400, "FAKE_CONNECTION_LOST");
        } else {
            out.write_raw(scratch.take());
        }
        return true;
    });
    auto options = options_for(dc, temp_dir_path("dl-resume.session"));
    options.retry.max_attempts = 1;  // a 400 is not retried anyway; keep the failure immediate
    Client client(options);
    client.connect();
    const std::string dest = temp_dir_path("resume.out");
    const auto msg = document_message(102, content);
    EXPECT_THROW(client.download_media(msg, dest), cppgram::mtproto::RpcError);
    EXPECT_TRUE(std::filesystem::exists(dest + ".part")) << "the partial file is kept for resume";
    EXPECT_TRUE(std::filesystem::exists(dest + ".part.meta"));
    EXPECT_EQ(std::filesystem::file_size(dest + ".part"), 2u << 20);

    rec.get_offsets.clear();
    EXPECT_EQ(client.download_media(msg, dest), static_cast<std::int64_t>(content.size()));
    ASSERT_FALSE(rec.get_offsets.empty());
    EXPECT_EQ(rec.get_offsets.front(), 2 << 20) << "resume starts at the first missing chunk";
    EXPECT_EQ(read_file(dest), content);
}

TEST(MediaDownload, ResumeIsRefusedForADifferentMedia) {
    FakeDc dc;
    Record rec;
    const std::string content = pattern(3 * 1024 * 1024, 14);
    std::atomic<bool> fail_once{true};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        TlWriter scratch;
        serve_document_part(args, content, offset, limit, scratch);
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            rec.get_offsets.push_back(offset);
        }
        if (offset == (1 << 20) && fail_once.exchange(false)) {
            answer_error(out, 400, "FAKE_CONNECTION_LOST");
        } else {
            out.write_raw(scratch.take());
        }
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-other.session")));
    client.connect();
    const std::string dest = temp_dir_path("other.out");
    EXPECT_THROW(client.download_media(document_message(200, content), dest), cppgram::mtproto::RpcError);
    rec.get_offsets.clear();
    client.download_media(document_message(201, content), dest);  // a different media id: must restart
    ASSERT_FALSE(rec.get_offsets.empty());
    EXPECT_EQ(rec.get_offsets.front(), 0);
    EXPECT_EQ(read_file(dest), content);
}

TEST(MediaDownload, ExpiredFileReferenceIsReportedClearly) {
    FakeDc dc;
    dc.set_handler([](std::uint32_t id, TlReader&, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        answer_error(out, 400, "FILE_REFERENCE_EXPIRED");
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-expired.session")));
    client.connect();
    const std::string content = pattern(4096, 15);
    try {
        client.download_media(document_message(300, content), temp_dir_path("expired.out"));
        FAIL() << "expected the download to fail";
    } catch (const cppgram::errors::FileTransferError& e) {
        EXPECT_NE(std::string(e.what()).find("file reference expired"), std::string::npos) << e.what();
    }
}

TEST(MediaDownload, SizeMismatchRemovesThePartialFiles) {
    FakeDc dc;
    const std::string served = pattern(1024 * 1024, 16);  // server has 1 MiB
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, served, offset, limit, out);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-mismatch.session")));
    client.connect();
    const std::string dest = temp_dir_path("mismatch.out");
    const std::string claimed = pattern(2 * 1024 * 1024, 16);  // the message claims 2 MiB
    EXPECT_THROW(client.download_media(document_message(400, claimed), dest), cppgram::errors::FileTransferError);
    EXPECT_FALSE(std::filesystem::exists(dest));
    EXPECT_FALSE(std::filesystem::exists(dest + ".part"));
    EXPECT_FALSE(std::filesystem::exists(dest + ".part.meta"));
}

TEST(MediaDownload, CancellationStopsBetweenChunks) {
    FakeDc dc;
    Record rec;
    const std::string content = pattern(4 * 1024 * 1024, 17);
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            rec.get_offsets.push_back(offset);
        }
        std::this_thread::sleep_for(100ms);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-cancel.session")));
    client.connect();
    const std::string dest = temp_dir_path("cancel.out");
    auto result = client.async([&](Client& c, std::stop_token) {
        return c.download_media(document_message(500, content), dest);
    });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            if (!rec.get_offsets.empty()) break;
        }
        std::this_thread::sleep_for(5ms);
    }
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    std::lock_guard<std::mutex> lock(rec.mutex);
    EXPECT_LT(rec.get_offsets.size(), 4u);
}

// ---- upload recovery after a dropped connection

TEST(MediaUploadRecovery, DroppedConnectionMidUploadReconnectsAndResendsThePart) {
    FakeDc dc;
    Record rec;
    std::atomic<bool> dropped{false};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        const std::int32_t part = record_part(id, args, rec, size);
        if (part == 1 && !dropped.exchange(true)) {
            // The connection goes away while the answer is being sent, so the client never receives it.
            dc.drop_connections_in_handler();
        }
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string content = pattern(3 * 1024, 21);
    const std::string path = temp_dir_path("recovery.bin");
    write_file(path, content);
    auto options = options_for(dc, temp_dir_path("recovery.session"));
    options.upload_part_size = 1024;
    options.auto_reconnect = true;
    Client client(options);
    client.connect();
    const int connections_before = dc.connections_accepted();

    EXPECT_NO_THROW(client.save_file(path));
    EXPECT_GT(dc.connections_accepted(), connections_before) << "the client must have reconnected";
    std::size_t bytes_sent = 0;
    int part1 = 0;
    for (const auto& [index, bytes] : rec.parts) {
        bytes_sent += bytes;
        if (index == 1) ++part1;
    }
    EXPECT_EQ(part1, 2) << "the lost part is resent once after the reconnect";
    EXPECT_EQ(bytes_sent, content.size() + 1024) << "every part is sent, and only the lost one twice";
}

TEST(MediaUploadRecovery, ReconnectThatNeverSucceedsEndsTheUploadWithAConnectionError) {
    FakeDc dc;
    Record rec;
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        std::size_t size = 0;
        const std::int32_t part = record_part(id, args, rec, size);
        if (part == 1) {
            dc.set_refusing(true);  // every later connection is closed straight after accept
            dc.drop_connections_in_handler();
        }
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string content = pattern(3 * 1024, 22);
    const std::string path = temp_dir_path("recovery_fail.bin");
    write_file(path, content);
    auto options = options_for(dc, temp_dir_path("recovery_fail.session"));
    options.upload_part_size = 1024;
    options.max_reconnects = 2;
    Client client(options);
    client.connect();

    try {
        client.save_file(path);
        FAIL() << "the upload must not report success when no reconnect succeeds";
    } catch (const std::exception& e) {
        EXPECT_EQ(cppgram::errors::categorize(e), cppgram::errors::Category::Network) << e.what();
    }
    int part1 = 0;
    for (const auto& [index, bytes] : rec.parts) {
        if (index == 1) ++part1;
    }
    EXPECT_EQ(part1, 1) << "a part that was never acknowledged is not resent on a dead session";
    dc.set_refusing(false);
}

// ---- download hardening

TEST(MediaDownloadHardening, SecondDownloadToTheSamePartialFileIsRefused) {
    FakeDc dc;
    const std::string content = pattern(2 * 1024 * 1024, 31);
    std::promise<void> gate;
    std::shared_future<void> gate_future = gate.get_future().share();
    std::atomic<bool> first_request_seen{false};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        if (!first_request_seen.exchange(true)) gate_future.wait();  // hold the first download mid-transfer
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        return true;
    });
    // The fake DC keeps one handshake state, so each client gets its own server. The lease is process-wide, so
    // the second client still hits the same partial file.
    FakeDc dc_second;
    Client first(options_for(dc, temp_dir_path("dl-lease-a.session")));
    Client second(options_for(dc_second, temp_dir_path("dl-lease-b.session")));
    first.connect();
    second.connect();
    const std::string dest = temp_dir_path("lease.out");
    const auto msg = document_message(700, content);

    std::thread writer([&] { EXPECT_EQ(first.download_media(msg, dest), static_cast<std::int64_t>(content.size())); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!first_request_seen.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(2ms);
    if (!first_request_seen.load()) {
        gate.set_value();  // release the writer before failing, so the join below cannot hang
        writer.join();
        FAIL() << "the first download never reached the server";
    }

    EXPECT_THROW(second.download_media(msg, dest), cppgram::errors::FileTransferError)
        << "two writers on one .part file would corrupt it";
    gate.set_value();
    writer.join();
    EXPECT_EQ(read_file(dest), content);
}

TEST(MediaDownloadHardening, InvalidOrStaleSidecarsAreDiscardedAndTheFileIsDownloadedAgain) {
    FakeDc dc;
    const std::string content = pattern(1500 * 1024, 33);
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-meta.session")));
    client.connect();
    const auto msg = document_message(800, content);

    // Garbage sidecar: the partial cannot be matched to this media, so it is replaced.
    const std::string dest_a = temp_dir_path("meta_bad.out");
    write_file(dest_a + ".part", std::string(2 * 1024 * 1024, 'x'));
    write_file(dest_a + ".part.meta", "not a fingerprint");
    EXPECT_EQ(client.download_media(msg, dest_a), static_cast<std::int64_t>(content.size()));
    EXPECT_EQ(read_file(dest_a), content);

    // Partial without any sidecar: it cannot be validated, so it is discarded.
    const std::string dest_b = temp_dir_path("meta_missing.out");
    write_file(dest_b + ".part", std::string(1024 * 1024, 'y'));
    EXPECT_EQ(client.download_media(msg, dest_b), static_cast<std::int64_t>(content.size()));
    EXPECT_EQ(read_file(dest_b), content);
    EXPECT_FALSE(std::filesystem::exists(dest_b + ".part.meta")) << "sidecars are removed after success";
}

TEST(MediaDownloadHardening, CancelledDownloadLeavesTheExistingDestinationUntouched) {
    FakeDc dc;
    Record rec;
    const std::string content = pattern(4 * 1024 * 1024, 35);
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        serve_document_part(args, content, offset, limit, out);
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            rec.get_offsets.push_back(offset);
        }
        std::this_thread::sleep_for(100ms);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-atomic.session")));
    client.connect();
    const std::string dest = temp_dir_path("atomic.out");
    write_file(dest, "previous contents");
    auto result = client.async([&](Client& c, std::stop_token) {
        return c.download_media(document_message(900, content), dest);
    });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(rec.mutex);
            if (!rec.get_offsets.empty()) break;
        }
        std::this_thread::sleep_for(5ms);
    }
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    EXPECT_EQ(read_file(dest), "previous contents") << "the destination changes only when a download completes";
    EXPECT_TRUE(std::filesystem::exists(dest + ".part")) << "the partial is kept for a later resume";
}

// ---- in-flight cancellation through Client (the request is on the wire when the cancel arrives)

TEST(MediaUploadRecovery, CancelDuringAnInFlightPartReturnsPromptlyAndTheClientStaysUsable) {
    FakeDc dc;
    std::promise<void> gate;
    std::shared_future<void> gate_future = gate.get_future().share();
    std::atomic<bool> held{false};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_saveFilePart::kId) return false;
        args.read_int64();
        args.read_int32();
        args.read_bytes();
        if (!held.exchange(true)) gate_future.wait();  // the reply is held until the test releases it
        out.write_uint32(kBoolTrue);
        return true;
    });
    const std::string content = pattern(2048, 41);
    const std::string path = temp_dir_path("inflight.bin");
    write_file(path, content);
    auto options = options_for(dc, temp_dir_path("inflight.session"));
    Client client(options);
    client.connect();

    auto result = client.async([&](Client& c, std::stop_token) { return c.save_file(path); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!held.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(2ms);
    ASSERT_TRUE(held.load());

    const auto begin = std::chrono::steady_clock::now();
    result.cancel();
    EXPECT_THROW(result.get(), cppgram::errors::CancelledError);
    // The session default request timeout is 30 s, so returning within 3 s proves the cancel ended the wait.
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 3s);

    gate.set_value();  // the late reply now reaches a session that no longer waits for it
    EXPECT_NO_THROW(client.save_file(path)) << "the stale reply must be skipped, not treated as this upload's answer";
}

TEST(MediaDownloadHardening, ATransientServerErrorOnOneChunkIsRetriedAndTheFileIsComplete) {
    FakeDc dc;
    const std::string content = pattern(2 * 1024 * 1024 + 300, 37);
    std::atomic<int> failures_left{1};
    dc.set_handler([&](std::uint32_t id, TlReader& args, TlWriter& out) {
        if (id != fn::upload_getFile::kId) return false;
        std::int64_t offset = 0;
        std::int32_t limit = 0;
        if (failures_left.load() > 0 && failures_left.fetch_sub(1) > 0) {
            // The first request fails once with a 500, whatever its offset; the client resends that chunk.
            args.read_uint32();
            args.read_uint32();
            args.read_int64();
            args.read_int64();
            args.read_bytes();
            args.read_string();
            args.read_int64();
            args.read_int32();
            answer_error(out, 500, "INTERNAL");
            return true;
        }
        serve_document_part(args, content, offset, limit, out);
        return true;
    });
    Client client(options_for(dc, temp_dir_path("dl-retry.session")));
    client.connect();
    const std::string dest = temp_dir_path("dl-retry.out");
    EXPECT_EQ(client.download_media(document_message(950, content), dest), static_cast<std::int64_t>(content.size()));
    EXPECT_EQ(read_file(dest), content);
    EXPECT_EQ(failures_left.load(), 0) << "the injected failure was actually served";
}

} // namespace
