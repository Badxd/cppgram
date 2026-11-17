// cppgram - TL (Type Language) binary serialization for MTProto.
#pragma once

#include <cstddef>
#include <utility>
#include <cstring>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cppgram::mtproto {

using Bytes = std::vector<std::uint8_t>;

class TlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::uint32_t kBoolFalseId = 0xbc799737u;
inline constexpr std::uint32_t kBoolTrueId = 0x997275b5u;
inline constexpr std::uint32_t kVectorId = 0x1cb5c415u;

class TlWriter {
public:
    void write_int32(std::int32_t value);
    void write_uint32(std::uint32_t value);
    void write_int64(std::int64_t value);
    void write_uint64(std::uint64_t value);
    void write_bool(bool value);
    void write_int128(const Bytes& value);
    void write_int256(const Bytes& value);
    void write_bytes(const Bytes& value);
    void write_string(std::string_view value);
    void write_raw(const Bytes& value);
    void write_vector_header(std::uint32_t count);
    void write_double(double value);

    const Bytes& bytes() const noexcept { return buffer_; }
    Bytes take() noexcept { return std::move(buffer_); }

private:
    Bytes buffer_;
};

class TlReader {
public:
    explicit TlReader(Bytes data) noexcept : data_(std::move(data)) {}

    std::uint32_t read_uint32();
    std::int32_t read_int32();
    std::uint64_t read_uint64();
    std::int64_t read_int64();
    bool read_bool();
    Bytes read_int128();
    Bytes read_int256();
    Bytes read_bytes();
    std::string read_string();
    Bytes read_raw(std::size_t size);
    double read_double();

    std::size_t remaining() const noexcept { return data_.size() - pos_; }
    std::size_t position() const noexcept { return pos_; }
    bool at_end() const noexcept { return pos_ == data_.size(); }

private:
    void require(std::size_t size) const;

    Bytes data_;
    std::size_t pos_ = 0;
};

namespace tl {

template <class T, class F>
std::vector<T> read_vector(TlReader& r, F&& f) {
    if (r.read_uint32() != kVectorId) {
        throw TlError("expected Vector constructor");
    }
    const std::uint32_t count = r.read_uint32();
    if (count > r.remaining()) {
        throw TlError("vector count exceeds available data");
    }
    std::vector<T> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(f(r));
    }
    return out;
}

template <class T>
std::shared_ptr<T> read_boxed(TlReader& r) {
    return std::make_shared<T>(T::read(r));
}

template <class C, class F>
void write_vector(TlWriter& w, const C& items, F&& f) {
    w.write_vector_header(static_cast<std::uint32_t>(items.size()));
    for (const auto& item : items) {
        f(w, item);
    }
}

}  // namespace tl

}  // namespace cppgram::mtproto

namespace cppgram::mtproto::tl {

// Raw TL object (`Object` in the schema). Only used for trailing fields such as rpc_result.result,
// where the object spans to the end of the enclosing message.
struct RawObject {
    Bytes data;

    void serialize(TlWriter& w) const { w.write_raw(data); }
};

}  // namespace cppgram::mtproto::tl

namespace cppgram::mtproto::tl {

// Bare vector (`vector<T>` in the schema): count and elements, without the Vector constructor id.
template <class T, class F>
std::vector<T> read_bare_vector(TlReader& r, F&& f) {
    const std::uint32_t count = r.read_uint32();
    if (count > r.remaining()) {
        throw TlError("bare vector count exceeds available data");
    }
    std::vector<T> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(f(r));
    }
    return out;
}

template <class C, class F>
void write_bare_vector(TlWriter& w, const C& items, F&& f) {
    w.write_uint32(static_cast<std::uint32_t>(items.size()));
    for (const auto& item : items) {
        f(w, item);
    }
}

}  // namespace cppgram::mtproto::tl
