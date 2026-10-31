#include "cppgram/transfer.h"

#include <stdexcept>

namespace cppgram {

namespace {

bool is_power_of_two(std::uint64_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

}  // namespace

UploadPlan plan_upload(std::uint64_t file_size, std::uint32_t part_size) {
    if (file_size == 0) throw std::invalid_argument("cannot upload an empty file");
    if (file_size > kMaxUploadFileSize) throw std::length_error("file is larger than the Telegram upload limit");
    if (part_size == 0) part_size = kMaxUploadPartSize;
    if (part_size > kMaxUploadPartSize) throw std::invalid_argument("upload part size exceeds 512 KiB");
    if (part_size % kUploadPartAlign != 0) throw std::invalid_argument("upload part size must be a multiple of 1 KiB");
    // 512 KiB must be divisible by the part size, which means the part size is a power of two.
    if (!is_power_of_two(part_size)) throw std::invalid_argument("upload part size must be a power of two");

    const std::uint64_t count = (file_size + part_size - 1) / part_size;
    if (count > kMaxUploadParts) {
        throw std::length_error("file needs more than 4000 parts at this part size; use a larger part size");
    }
    UploadPlan plan;
    plan.part_size = part_size;
    plan.part_count = static_cast<std::uint32_t>(count);
    plan.last_part_size = file_size - (count - 1) * part_size;
    plan.big_file = file_size > kBigFileThreshold;
    return plan;
}

std::uint64_t upload_part_offset(const UploadPlan& plan, std::uint32_t index) {
    if (index >= plan.part_count) throw std::out_of_range("upload part index out of range");
    return static_cast<std::uint64_t>(index) * plan.part_size;
}

std::uint64_t download_chunk_count(std::uint64_t file_size, std::uint32_t limit) {
    if (limit == 0 || limit > 1024u * 1024u || limit % 4096u != 0 || !is_power_of_two(limit)) {
        throw std::invalid_argument("download limit must be a power of two, a multiple of 4 KiB, at most 1 MiB");
    }
    return (file_size + limit - 1) / limit;
}

std::string sanitize_remote_filename(std::string_view name, std::size_t max_len) {
    // Keep only the last component, treating both separators as separators.
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name = name.substr(slash + 1);

    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) continue;  // control characters
        out.push_back(static_cast<char>(c));
    }
    // No hidden files, no "." or "..", no empty names.
    std::size_t start = 0;
    while (start < out.size() && out[start] == '.') ++start;
    out.erase(0, start);
    if (out.empty()) return "file";

    if (max_len == 0) max_len = 1;
    if (out.size() > max_len) {
        const std::size_t dot = out.find_last_of('.');
        const bool has_ext = dot != std::string::npos && dot > 0 && out.size() - dot <= 16;
        if (has_ext) {
            const std::string ext = out.substr(dot);
            out = out.substr(0, max_len - ext.size()) + ext;
        } else {
            out.resize(max_len);
        }
    }
    return out;
}

}  // namespace cppgram
