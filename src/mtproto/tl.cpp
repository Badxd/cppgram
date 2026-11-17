#include "cppgram/mtproto/tl.h"

namespace cppgram::mtproto {

namespace {

void put_u32_le(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void put_u64_le(Bytes& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

std::uint64_t get_le(const Bytes& data, std::size_t pos, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(data[pos + i]) << (8 * i);
    }
    return value;
}

}  // namespace

void TlWriter::write_int32(std::int32_t value) {
    put_u32_le(buffer_, static_cast<std::uint32_t>(value));
}

void TlWriter::write_uint32(std::uint32_t value) {
    put_u32_le(buffer_, value);
}

void TlWriter::write_int64(std::int64_t value) {
    put_u64_le(buffer_, static_cast<std::uint64_t>(value));
}

void TlWriter::write_uint64(std::uint64_t value) {
    put_u64_le(buffer_, value);
}

void TlWriter::write_bool(bool value) {
    write_uint32(value ? kBoolTrueId : kBoolFalseId);
}

void TlWriter::write_int128(const Bytes& value) {
    if (value.size() != 16) {
        throw TlError("int128 must be exactly 16 bytes");
    }
    write_raw(value);
}

void TlWriter::write_int256(const Bytes& value) {
    if (value.size() != 32) {
        throw TlError("int256 must be exactly 32 bytes");
    }
    write_raw(value);
}

void TlWriter::write_bytes(const Bytes& value) {
    const std::size_t size = value.size();
    if (size <= 253) {
        buffer_.push_back(static_cast<std::uint8_t>(size));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
        std::size_t total = 1 + size;
        while (total % 4 != 0) {
            buffer_.push_back(0);
            ++total;
        }
    } else {
        if (size >= (1u << 24)) {
            throw TlError("bytes value too large for TL encoding");
        }
        buffer_.push_back(0xfe);
        buffer_.push_back(static_cast<std::uint8_t>(size));
        buffer_.push_back(static_cast<std::uint8_t>(size >> 8));
        buffer_.push_back(static_cast<std::uint8_t>(size >> 16));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
        std::size_t total = 4 + size;
        while (total % 4 != 0) {
            buffer_.push_back(0);
            ++total;
        }
    }
}

void TlWriter::write_string(std::string_view value) {
    write_bytes(Bytes(value.begin(), value.end()));
}

void TlWriter::write_raw(const Bytes& value) {
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void TlWriter::write_vector_header(std::uint32_t count) {
    write_uint32(kVectorId);
    write_uint32(count);
}

void TlWriter::write_double(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_uint64(bits);
}

double TlReader::read_double() {
    const std::uint64_t bits = read_uint64();
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TlReader::require(std::size_t size) const {
    if (remaining() < size) {
        throw TlError("unexpected end of TL data");
    }
}

std::uint32_t TlReader::read_uint32() {
    require(4);
    const auto value = static_cast<std::uint32_t>(get_le(data_, pos_, 4));
    pos_ += 4;
    return value;
}

std::int32_t TlReader::read_int32() {
    return static_cast<std::int32_t>(read_uint32());
}

std::uint64_t TlReader::read_uint64() {
    require(8);
    const auto value = get_le(data_, pos_, 8);
    pos_ += 8;
    return value;
}

std::int64_t TlReader::read_int64() {
    return static_cast<std::int64_t>(read_uint64());
}

bool TlReader::read_bool() {
    const auto id = read_uint32();
    if (id == kBoolTrueId) {
        return true;
    }
    if (id == kBoolFalseId) {
        return false;
    }
    throw TlError("invalid Bool constructor id");
}

Bytes TlReader::read_int128() {
    return read_raw(16);
}

Bytes TlReader::read_int256() {
    return read_raw(32);
}

Bytes TlReader::read_raw(std::size_t size) {
    require(size);
    Bytes out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
              data_.begin() + static_cast<std::ptrdiff_t>(pos_ + size));
    pos_ += size;
    return out;
}

Bytes TlReader::read_bytes() {
    require(1);
    std::size_t size = data_[pos_];
    std::size_t header = 1;
    if (size == 0xfe) {
        require(4);
        size = static_cast<std::size_t>(get_le(data_, pos_ + 1, 3));
        header = 4;
    }
    const std::size_t total = header + size;
    const std::size_t padded = (total + 3) / 4 * 4;
    require(padded);
    Bytes out(data_.begin() + static_cast<std::ptrdiff_t>(pos_ + header),
              data_.begin() + static_cast<std::ptrdiff_t>(pos_ + total));
    pos_ += padded;
    return out;
}

std::string TlReader::read_string() {
    const Bytes raw = read_bytes();
    return std::string(raw.begin(), raw.end());
}

}  // namespace cppgram::mtproto
