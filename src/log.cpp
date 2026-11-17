#include "cppgram/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>

namespace cppgram::log {
namespace {

std::atomic<Level> g_level { Level::Warn };
std::mutex g_sink_mutex;
Sink g_sink;

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool is_token_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Masks "<digits>:<30+ token chars>" as Telegram bot tokens look.
std::string mask_bot_tokens(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (std::isdigit(static_cast<unsigned char>(text[i]))
            && (i == 0 || !std::isdigit(static_cast<unsigned char>(text[i - 1])))) {
            std::size_t j = i;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])))
                ++j;
            if (j < text.size() && text[j] == ':' && j - i >= 5) {
                std::size_t k = j + 1;
                while (k < text.size() && is_token_char(text[k]))
                    ++k;
                if (k - (j + 1) >= 30) {
                    out += "<bot-token>";
                    i = k;
                    continue;
                }
            }
            out.append(text.substr(i, j - i));
            i = j;
            continue;
        }
        out += text[i];
        ++i;
    }
    return out;
}

// Masks the value of "key=value" pairs in free text when the key is sensitive. Values end at whitespace or '&'.
std::string mask_key_values(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '=' && i > 0) {
            std::size_t start = i;
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_'))
                --start;
            const std::string key(text.substr(start, i - start));
            out.resize(out.size() - key.size());
            out += key;
            out += '=';
            std::size_t end = i + 1;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '&')
                ++end;
            if (is_sensitive_key(key)) {
                out += "***";
            } else {
                out.append(text.substr(i + 1, end - (i + 1)));
            }
            i = end;
            continue;
        }
        out += text[i];
        ++i;
    }
    return out;
}

} // namespace

std::string_view to_string(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    case Level::Off:
        return "OFF";
    }
    return "OFF";
}

std::optional<Level> parse_level(std::string_view name) noexcept {
    const std::string n = lower(name);
    if (n == "trace")
        return Level::Trace;
    if (n == "debug")
        return Level::Debug;
    if (n == "info")
        return Level::Info;
    if (n == "warn" || n == "warning")
        return Level::Warn;
    if (n == "error")
        return Level::Error;
    if (n == "off")
        return Level::Off;
    return std::nullopt;
}

void set_level(Level level) noexcept {
    g_level.store(level, std::memory_order_relaxed);
}

Level level() noexcept {
    return g_level.load(std::memory_order_relaxed);
}

bool enabled(Level level) noexcept {
    const Level current = g_level.load(std::memory_order_relaxed);
    return current != Level::Off && level != Level::Off && static_cast<int>(level) >= static_cast<int>(current);
}

void set_sink(Sink sink) {
    std::lock_guard lock(g_sink_mutex);
    g_sink = std::move(sink);
}

bool is_sensitive_key(std::string_view key) noexcept {
    static constexpr std::array<std::string_view, 13> sensitive {
        "api_hash", "apihash",        "bot_token", "token",  "password", "code", "otp",
        "session",  "session_string", "auth_key",  "secret", "srp",      "hash"
    };
    const std::string k = lower(key);
    return std::find(sensitive.begin(), sensitive.end(), std::string_view(k)) != sensitive.end() || k == "phone_code";
}

std::string redact(std::string_view text) {
    return mask_key_values(mask_bot_tokens(text));
}

std::string format(const Record& record) {
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(record.time);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(record.time - secs).count();
    const std::time_t t = std::chrono::system_clock::to_time_t(record.time);
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ", utc.tm_year + 1900, utc.tm_mon + 1,
                  utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<long long>(millis));

    std::string line = stamp;
    line += ' ';
    const std::string_view name = to_string(record.level);
    line.append(name);
    line.append(5 - name.size(), ' ');
    line += " [";
    line += record.component;
    line += "] ";
    if (!record.request_id.empty()) {
        line += "rid=";
        line += record.request_id;
        line += ' ';
    }
    line += record.message;
    for (const auto& f : record.fields) {
        line += ' ';
        line += f.key;
        line += '=';
        line += f.value;
    }
    return line;
}

void write(Level lvl, std::string_view component, std::string_view message, std::initializer_list<Field> fields,
           std::string_view request_id) {
    if (!enabled(lvl))
        return;

    Record record;
    record.time = std::chrono::system_clock::now();
    record.level = lvl;
    record.component = std::string(component);
    record.request_id = redact(request_id);
    record.message = redact(message);
    record.fields.reserve(fields.size());
    for (const auto& f : fields) {
        record.fields.push_back({ f.key, is_sensitive_key(f.key) ? std::string("***") : redact(f.value) });
    }

    std::lock_guard lock(g_sink_mutex);
    if (g_sink) {
        g_sink(record);
    } else {
        std::clog << format(record) << '\n';
    }
}

} // namespace cppgram::log
