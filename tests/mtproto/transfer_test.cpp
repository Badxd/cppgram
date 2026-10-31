#include <gtest/gtest.h>

#include <stdexcept>

#include "cppgram/transfer.h"

using namespace cppgram;

TEST(TransferPlan, SmallFileIsOnePartWithExactLastSize) {
    const auto p = plan_upload(1000);
    EXPECT_EQ(p.part_size, kMaxUploadPartSize);
    EXPECT_EQ(p.part_count, 1u);
    EXPECT_EQ(p.last_part_size, 1000u);
    EXPECT_FALSE(p.big_file);
}

TEST(TransferPlan, PartBoundariesAreExact) {
    const std::uint64_t part = kMaxUploadPartSize;
    EXPECT_EQ(plan_upload(part).part_count, 1u);
    EXPECT_EQ(plan_upload(part + 1).part_count, 2u);
    EXPECT_EQ(plan_upload(part + 1).last_part_size, 1u);
    EXPECT_EQ(plan_upload(3 * part).last_part_size, part);
}

TEST(TransferPlan, BigFileThreshold) {
    EXPECT_FALSE(plan_upload(kBigFileThreshold).big_file);
    EXPECT_TRUE(plan_upload(kBigFileThreshold + 1).big_file);
}

TEST(TransferPlan, RejectsEmptyFile) {
    EXPECT_THROW(plan_upload(0), std::invalid_argument);
}

TEST(TransferPlan, RejectsBadPartSizes) {
    EXPECT_THROW(plan_upload(10, kMaxUploadPartSize + 1024), std::invalid_argument);  // above 512 KiB
    EXPECT_THROW(plan_upload(10, 1000), std::invalid_argument);                       // not 1 KiB aligned
    EXPECT_THROW(plan_upload(10, 3 * 1024), std::invalid_argument);                   // not a power of two
    EXPECT_NO_THROW(plan_upload(10, 1024));
    EXPECT_NO_THROW(plan_upload(10, 256 * 1024));
}

TEST(TransferPlan, RejectsTooManyParts) {
    // 4000 parts of 512 KiB is about 2000 MiB; one byte more than that needs 4001 parts.
    const std::uint64_t max_size = static_cast<std::uint64_t>(kMaxUploadParts) * kMaxUploadPartSize;
    EXPECT_NO_THROW(plan_upload(max_size));
    EXPECT_THROW(plan_upload(max_size + 1), std::length_error);
    // A smaller part size cannot fix a file that is over the overall limit.
    EXPECT_THROW(plan_upload(kMaxUploadFileSize + 1), std::length_error);
}

TEST(TransferPlan, OffsetsCoverTheWholeFile) {
    const std::uint32_t part = 256 * 1024;  // 1 MiB is above the 512 KiB Telegram limit
    const auto p = plan_upload(5 * 1024 * 1024 + 7, part);
    EXPECT_EQ(p.part_count, 21u);            // 20 full parts of 256 KiB plus one 7-byte part
    EXPECT_EQ(upload_part_offset(p, 0), 0u);
    EXPECT_EQ(upload_part_offset(p, 5), 5u * part);
    EXPECT_EQ(upload_part_offset(p, 20), 20u * part);
    EXPECT_EQ(p.last_part_size, 7u);
    EXPECT_THROW(upload_part_offset(p, 21), std::out_of_range);
    EXPECT_THROW(plan_upload(5 * 1024 * 1024, 1024 * 1024), std::invalid_argument);
}

TEST(TransferPlan, DownloadChunkCount) {
    EXPECT_EQ(download_chunk_count(0, 4096), 0u);
    EXPECT_EQ(download_chunk_count(1, 4096), 1u);
    EXPECT_EQ(download_chunk_count(4096, 4096), 1u);
    EXPECT_EQ(download_chunk_count(4097, 4096), 2u);
    EXPECT_THROW(download_chunk_count(10, 0), std::invalid_argument);
    EXPECT_THROW(download_chunk_count(10, 3000), std::invalid_argument);   // not 4 KiB aligned
    EXPECT_THROW(download_chunk_count(10, 8192 * 3), std::invalid_argument);  // not a power of two
    EXPECT_THROW(download_chunk_count(10, 2 * 1024 * 1024), std::invalid_argument);  // above 1 MiB
}

TEST(FileNameSafety, KeepsPlainNames) {
    EXPECT_EQ(sanitize_remote_filename("report.pdf"), "report.pdf");
    EXPECT_EQ(sanitize_remote_filename("my photo 1.jpg"), "my photo 1.jpg");
}

TEST(FileNameSafety, RemovesPathComponents) {
    EXPECT_EQ(sanitize_remote_filename("../../etc/passwd"), "passwd");
    EXPECT_EQ(sanitize_remote_filename("/root/.bashrc"), "bashrc");
    EXPECT_EQ(sanitize_remote_filename("..\\..\\windows\\system.ini"), "system.ini");
    EXPECT_EQ(sanitize_remote_filename("dir/sub/"), "file");
}

TEST(FileNameSafety, RejectsDotNamesAndHiddenFiles) {
    EXPECT_EQ(sanitize_remote_filename(".."), "file");
    EXPECT_EQ(sanitize_remote_filename("."), "file");
    EXPECT_EQ(sanitize_remote_filename("...."), "file");
    EXPECT_EQ(sanitize_remote_filename(".ssh"), "ssh");
    EXPECT_EQ(sanitize_remote_filename(""), "file");
}

TEST(FileNameSafety, DropsControlCharacters) {
    const std::string name = std::string("a\x01" "b\nc\x7f" "d.txt");
    EXPECT_EQ(sanitize_remote_filename(name), "abcd.txt");
}

TEST(FileNameSafety, CapsLengthAndKeepsExtension) {
    const std::string long_name = std::string(400, 'a') + ".mp4";
    const std::string out = sanitize_remote_filename(long_name, 255);
    EXPECT_EQ(out.size(), 255u);
    EXPECT_EQ(out.substr(out.size() - 4), ".mp4");

    const std::string no_ext(300, 'b');
    EXPECT_EQ(sanitize_remote_filename(no_ext, 100).size(), 100u);
}
