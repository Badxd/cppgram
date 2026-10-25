#include "cppgram/mtproto/transport.h"

#include "cppgram/mtproto/crypto.h"

namespace cppgram::mtproto {

namespace {

constexpr std::size_t kMaxFrame = 16u * 1024u * 1024u;

void put_le32(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

std::uint32_t get_le32(const Bytes& data, std::size_t pos) {
    return static_cast<std::uint32_t>(data[pos]) | (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
           (static_cast<std::uint32_t>(data[pos + 2]) << 16) | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
}

}  // namespace

Bytes Transport::init_bytes() const {
    switch (kind_) {
        case TransportKind::Abridged:
            return {0xef};
        case TransportKind::Intermediate:
            return {0xee, 0xee, 0xee, 0xee};
        case TransportKind::Full:
            return {};
    }
    return {};
}

Bytes Transport::encode(const Bytes& payload) {
    Bytes out;
    switch (kind_) {
        case TransportKind::Abridged: {
            if (payload.size() % 4 != 0) {
                throw TransportError("abridged payload must be a multiple of 4 bytes");
            }
            const std::size_t words = payload.size() / 4;
            if (words < 0x7f) {
                out.push_back(static_cast<std::uint8_t>(words));
            } else if (words < (1u << 24)) {
                out.push_back(0x7f);
                out.push_back(static_cast<std::uint8_t>(words));
                out.push_back(static_cast<std::uint8_t>(words >> 8));
                out.push_back(static_cast<std::uint8_t>(words >> 16));
            } else {
                throw TransportError("abridged payload too large");
            }
            break;
        }
        case TransportKind::Intermediate: {
            if (payload.size() >= kMaxFrame) {
                throw TransportError("intermediate payload too large");
            }
            put_le32(out, static_cast<std::uint32_t>(payload.size()));
            break;
        }
        case TransportKind::Full: {
            const std::uint32_t total = static_cast<std::uint32_t>(payload.size() + 12);
            if (total >= kMaxFrame) {
                throw TransportError("full payload too large");
            }
            put_le32(out, total);
            put_le32(out, send_seq_++);
            break;
        }
    }
    out.insert(out.end(), payload.begin(), payload.end());
    if (kind_ == TransportKind::Full) {
        const std::uint32_t crc = crc32(out);
        put_le32(out, crc);
    }
    return out;
}

void Transport::feed(const Bytes& data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::optional<Bytes> Transport::next() {
    // Compact the buffer once the consumed prefix gets large.
    if (consumed_ > 0 && consumed_ * 2 >= buffer_.size()) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0;
    }
    const std::size_t available = buffer_.size() - consumed_;
    const std::size_t base = consumed_;

    switch (kind_) {
        case TransportKind::Abridged: {
            if (available < 1) {
                return std::nullopt;
            }
            std::size_t header = 1;
            std::size_t words = buffer_[base] & 0x7fu;
            if (words == 0x7f) {
                if (available < 4) {
                    return std::nullopt;
                }
                header = 4;
                words = static_cast<std::size_t>(buffer_[base + 1]) |
                        (static_cast<std::size_t>(buffer_[base + 2]) << 8) |
                        (static_cast<std::size_t>(buffer_[base + 3]) << 16);
            }
            const std::size_t size = words * 4;
            if (size > kMaxFrame) {
                throw TransportError("abridged frame too large");
            }
            if (available < header + size) {
                return std::nullopt;
            }
            Bytes payload(buffer_.begin() + static_cast<std::ptrdiff_t>(base + header),
                          buffer_.begin() + static_cast<std::ptrdiff_t>(base + header + size));
            consumed_ += header + size;
            return payload;
        }
        case TransportKind::Intermediate: {
            if (available < 4) {
                return std::nullopt;
            }
            const std::size_t size = get_le32(buffer_, base);
            if (size > kMaxFrame) {
                // Telegram sends small negative int32 codes (for example -404) in place of a length. Show the raw word.
                char hex[32];
                std::snprintf(hex, sizeof hex, "0x%08zx", size);
                throw TransportError(std::string("intermediate frame too large (length word ") + hex + ", likely a transport error code from the server)");
            }
            if (available < 4 + size) {
                return std::nullopt;
            }
            Bytes payload(buffer_.begin() + static_cast<std::ptrdiff_t>(base + 4),
                          buffer_.begin() + static_cast<std::ptrdiff_t>(base + 4 + size));
            consumed_ += 4 + size;
            return payload;
        }
        case TransportKind::Full: {
            if (available < 4) {
                return std::nullopt;
            }
            const std::size_t total = get_le32(buffer_, base);
            if (total < 12 || total > kMaxFrame) {
                throw TransportError("invalid full frame length");
            }
            if (available < total) {
                return std::nullopt;
            }
            const Bytes frame(buffer_.begin() + static_cast<std::ptrdiff_t>(base),
                              buffer_.begin() + static_cast<std::ptrdiff_t>(base + total));
            const std::uint32_t expected = get_le32(frame, total - 4);
            const std::uint32_t actual = crc32(slice(frame, 0, total - 4));
            if (expected != actual) {
                throw TransportError("full frame crc mismatch");
            }
            consumed_ += total;
            return slice(frame, 8, total - 12);
        }
    }
    return std::nullopt;
}

}  // namespace cppgram::mtproto
