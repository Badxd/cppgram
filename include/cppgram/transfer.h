// cppgram - file transfer planning and remote file name safety.
// Pure functions: part sizes, part counts and offsets follow Telegram's upload.saveFilePart rules,
// so the chunk math can be tested without a connection or a real file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cppgram {

// Telegram limits (upload.saveFilePart / upload.saveBigFilePart, documented at core.telegram.org/api/files).
inline constexpr std::uint32_t kMaxUploadPartSize = 512u * 1024u;
inline constexpr std::uint32_t kUploadPartAlign = 1024u;
inline constexpr std::uint64_t kBigFileThreshold = 10ull * 1024ull * 1024ull;
inline constexpr std::uint32_t kMaxUploadParts = 4000;
inline constexpr std::uint64_t kMaxUploadFileSize = 2000ull * 1024ull * 1024ull;

struct UploadPlan {
    std::uint32_t part_size = 0;     // bytes per part, except the last one
    std::uint32_t part_count = 0;    // total parts, at least 1
    std::uint64_t last_part_size = 0;
    bool big_file = false;           // true selects upload.saveBigFilePart
};

// Plans an upload of file_size bytes. part_size 0 picks a default (512 KiB, the largest allowed).
// Throws std::invalid_argument for an empty file, a bad part size or a file over kMaxUploadFileSize,
// and std::length_error when the file would need more than kMaxUploadParts parts.
UploadPlan plan_upload(std::uint64_t file_size, std::uint32_t part_size = 0);

// Byte offset of part `index` (0-based) in a plan.
std::uint64_t upload_part_offset(const UploadPlan& plan, std::uint32_t index);

// Download chunks: upload.getFile limit is a power of two, a multiple of 4 KiB, at most 1 MiB.
// Returns the number of chunks needed to read file_size bytes. Throws std::invalid_argument on a bad limit.
std::uint64_t download_chunk_count(std::uint64_t file_size, std::uint32_t limit);

// Makes a remote file name safe to use as a local file name: keeps only the last path component,
// removes control characters and path separators, rejects "." and "..", strips leading dots (no hidden
// files), caps the length at max_len bytes while keeping the extension, and falls back to "file".
std::string sanitize_remote_filename(std::string_view name, std::size_t max_len = 255);

}  // namespace cppgram
