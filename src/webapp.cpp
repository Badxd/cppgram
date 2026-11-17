#include "cppgram/webapp.h"
#include "cppgram/log.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/params.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace cppgram::webapp {
namespace {

constexpr std::string_view kSecretKeyLabel = "WebAppData";

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Percent-decodes one query component. '+' is a space. Returns false on a bad escape.
bool percent_decode(std::string_view in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%') {
            if (i + 2 >= in.size())
                return false;
            const int hi = hex_value(in[i + 1]);
            const int lo = hex_value(in[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            out += static_cast<char>((hi << 4) | lo);
            i += 2;
        } else {
            out += c;
        }
    }
    return true;
}

bool is_hash_hex(std::string_view s) noexcept {
    if (s.size() != 64)
        return false;
    for (char c : s) {
        if (hex_value(c) < 0)
            return false;
    }
    return true;
}

std::string lowercase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Parses a decimal auth_date. Digits only, at most 18 characters, so it always fits in int64.
bool parse_unix_time(std::string_view s, std::int64_t& out) noexcept {
    if (s.empty() || s.size() > 18)
        return false;
    std::int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

std::optional<std::string> optional_field(const std::map<std::string, std::string>& fields, const char* key) {
    const auto it = fields.find(key);
    if (it == fields.end())
        return std::nullopt;
    return it->second;
}

// Returns false if the user JSON is not an object with an integer id and a first_name.
bool parse_user(const std::string& json_text, User& out) {
    const auto j = nlohmann::json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return false;
    const auto id = j.find("id");
    const auto first = j.find("first_name");
    if (id == j.end() || !id->is_number_integer())
        return false;
    if (first == j.end() || !first->is_string())
        return false;
    out.id = id->get<std::int64_t>();
    out.first_name = first->get<std::string>();
    if (const auto it = j.find("is_bot"); it != j.end() && it->is_boolean())
        out.is_bot = it->get<bool>();
    if (const auto it = j.find("last_name"); it != j.end() && it->is_string())
        out.last_name = it->get<std::string>();
    if (const auto it = j.find("username"); it != j.end() && it->is_string())
        out.username = it->get<std::string>();
    if (const auto it = j.find("language_code"); it != j.end() && it->is_string()) {
        out.language_code = it->get<std::string>();
    }
    return true;
}

// Raw HMAC-SHA256. Returns an empty string on failure, which never equals a valid hash.
std::string hmac_sha256_raw(std::string_view key, std::string_view message) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
    if (!mac)
        return {};
    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(EVP_MAC_CTX_new(mac.get()), EVP_MAC_CTX_free);
    if (!ctx)
        return {};
    char digest[] = "SHA256";
    OSSL_PARAM params[] = { OSSL_PARAM_construct_utf8_string("digest", digest, 0), OSSL_PARAM_construct_end() };
    if (EVP_MAC_init(ctx.get(), reinterpret_cast<const unsigned char*>(key.data()), key.size(), params) != 1)
        return {};
    if (EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) != 1)
        return {};
    unsigned char out[EVP_MAX_MD_SIZE];
    std::size_t len = 0;
    if (EVP_MAC_final(ctx.get(), out, &len, sizeof out) != 1)
        return {};
    return std::string(reinterpret_cast<const char*>(out), len);
#else
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(message.data()), message.size(), out, &len)
        == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(out), len);
#endif
}

std::string to_hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 0x0f];
    }
    return out;
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::string_view to_string(Status status) noexcept {
    switch (status) {
    case Status::Valid:
        return "valid";
    case Status::Empty:
        return "empty";
    case Status::Malformed:
        return "malformed";
    case Status::BadEncoding:
        return "bad_encoding";
    case Status::DuplicateField:
        return "duplicate_field";
    case Status::MissingHash:
        return "missing_hash";
    case Status::MissingAuthDate:
        return "missing_auth_date";
    case Status::BadAuthDate:
        return "bad_auth_date";
    case Status::BadSignature:
        return "bad_signature";
    case Status::Expired:
        return "expired";
    case Status::FromFuture:
        return "from_future";
    case Status::BadUser:
        return "bad_user";
    }
    return "malformed";
}

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
    return to_hex(hmac_sha256_raw(key, message));
}

static Result verify_unlogged(std::string_view init_data, std::string_view bot_token, std::int64_t now_unix,
                              const VerifyOptions& options) {
    Result result;

    std::string_view query = init_data;
    if (!query.empty() && query.front() == '?')
        query.remove_prefix(1);
    if (query.empty()) {
        result.status = Status::Empty;
        return result;
    }

    // 1. Split and decode. Any repeated key is rejected, not just the security-sensitive ones.
    std::map<std::string, std::string> fields;
    std::size_t pos = 0;
    while (true) {
        const std::size_t amp = query.find('&', pos);
        const std::string_view segment
            = query.substr(pos, amp == std::string_view::npos ? std::string_view::npos : amp - pos);
        const std::size_t eq = segment.find('=');
        if (segment.empty() || eq == std::string_view::npos || eq == 0) {
            result.status = Status::Malformed;
            return result;
        }
        std::string key;
        std::string value;
        if (!percent_decode(segment.substr(0, eq), key) || !percent_decode(segment.substr(eq + 1), value)) {
            result.status = Status::BadEncoding;
            return result;
        }
        if (key.empty()) {
            result.status = Status::Malformed;
            return result;
        }
        if (!fields.emplace(std::move(key), std::move(value)).second) {
            result.status = Status::DuplicateField;
            return result;
        }
        if (amp == std::string_view::npos)
            break;
        pos = amp + 1;
    }

    // 2. Required fields.
    const auto hash_it = fields.find("hash");
    if (hash_it == fields.end()) {
        result.status = Status::MissingHash;
        return result;
    }
    if (!is_hash_hex(hash_it->second)) {
        result.status = Status::Malformed;
        return result;
    }
    const auto auth_it = fields.find("auth_date");
    if (auth_it == fields.end()) {
        result.status = Status::MissingAuthDate;
        return result;
    }
    std::int64_t auth_date = 0;
    if (!parse_unix_time(auth_it->second, auth_date)) {
        result.status = Status::BadAuthDate;
        return result;
    }

    // 3. data-check-string: every field except hash, sorted by key (std::map order), "key=value", joined by '\n'.
    std::string data_check_string;
    for (const auto& [key, value] : fields) {
        if (key == "hash")
            continue;
        if (!data_check_string.empty())
            data_check_string += '\n';
        data_check_string += key;
        data_check_string += '=';
        data_check_string += value;
    }

    // 4. Signature. An empty token cannot produce a matching signature.
    if (bot_token.empty()) {
        result.status = Status::BadSignature;
        return result;
    }
    const std::string secret_key = hmac_sha256_raw(kSecretKeyLabel, bot_token);
    const std::string expected = to_hex(hmac_sha256_raw(secret_key, data_check_string));
    if (expected.empty() || !constant_time_equal(lowercase(hash_it->second), expected)) {
        result.status = Status::BadSignature;
        return result;
    }

    // 5. Time window. Checked only after the signature, so unsigned data never reaches these branches.
    if (auth_date - now_unix > options.max_future_skew.count()) {
        result.status = Status::FromFuture;
        return result;
    }
    if (now_unix - auth_date > options.max_age.count()) {
        result.status = Status::Expired;
        return result;
    }

    // 6. Build the verified result.
    InitData data;
    data.auth_date = auth_date;
    data.query_id = optional_field(fields, "query_id");
    data.chat_type = optional_field(fields, "chat_type");
    data.chat_instance = optional_field(fields, "chat_instance");
    data.start_param = optional_field(fields, "start_param");
    if (const auto user_it = fields.find("user"); user_it != fields.end()) {
        User user;
        if (!parse_user(user_it->second, user)) {
            result.status = Status::BadUser;
            return result;
        }
        data.user = std::move(user);
    }
    for (const auto& [key, value] : fields) {
        if (key != "hash")
            data.fields.emplace(key, value);
    }

    result.status = Status::Valid;
    result.data = std::move(data);
    return result;
}

// Public entry point with an explicit clock. Logs only the outcome status; the token and the user data never reach a log.
Result verify(std::string_view init_data, std::string_view bot_token, std::int64_t now_unix,
              const VerifyOptions& options) {
    Result result = verify_unlogged(init_data, bot_token, now_unix, options);
    if (result.ok()) {
        log::write(log::Level::Debug, "webapp", "initData accepted");
    } else {
        log::write(log::Level::Warn, "webapp", "initData rejected",
                   {{"status", std::string(to_string(result.status))}});
    }
    return result;
}

Result verify(std::string_view init_data, std::string_view bot_token, const VerifyOptions& options) {
    return verify(init_data, bot_token, now_seconds(), options);
}

InitData verify_or_throw(std::string_view init_data, std::string_view bot_token, const VerifyOptions& options) {
    Result result = verify(init_data, bot_token, options);
    if (!result.ok()) {
        throw errors::AuthenticationError(std::string("web app initData rejected: ")
                                          + std::string(to_string(result.status)));
    }
    return std::move(*result.data);
}

} // namespace cppgram::webapp
