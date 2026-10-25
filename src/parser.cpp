#include "cppgram/parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cppgram {

namespace {

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Appends UTF-8 text and tracks the UTF-16 length of what was produced so far.
struct Builder {
    std::string text;
    std::int32_t units = 0;
    std::vector<TextEntity> entities;

    void add_bytes(std::string_view bytes) {
        text.append(bytes.data(), bytes.size());
        units += static_cast<std::int32_t>(utf16_length(bytes));
    }
    void add_char(char c) {
        text.push_back(c);
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++units;
        // continuation bytes add nothing; a 4-byte sequence counts as 2 units through its lead byte
        if (static_cast<unsigned char>(c) >= 0xF0) ++units;
    }
    void add_codepoint(std::uint32_t cp) {
        append_utf8(text, cp);
        units += cp >= 0x10000 ? 2 : 1;
    }
    void push_entity(TextEntity::Kind kind, std::int32_t start, std::string url = {}, std::string language = {},
                     std::int64_t user_id = 0) {
        if (units <= start) return;
        TextEntity e;
        e.kind = kind;
        e.offset = start;
        e.length = units - start;
        e.url = std::move(url);
        e.language = std::move(language);
        e.user_id = user_id;
        entities.push_back(std::move(e));
    }
};

// Decodes one character entity starting at input[i] == '&'. Returns the code point and the
// index after it, or 0 if this is not a recognised entity.
std::uint32_t decode_entity(std::string_view s, std::size_t& i) {
    const std::size_t semi = s.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) return 0;
    const std::string_view name = s.substr(i + 1, semi - i - 1);
    std::uint32_t cp = 0;
    if (name == "amp") cp = '&';
    else if (name == "lt") cp = '<';
    else if (name == "gt") cp = '>';
    else if (name == "quot") cp = '"';
    else if (name == "apos") cp = '\'';
    else if (name == "nbsp") cp = 0xA0;
    else if (!name.empty() && name[0] == '#') {
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const std::string digits(name.substr(hex ? 2 : 1));
        if (digits.empty()) return 0;
        for (char c : digits) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return 0;
        }
        cp = static_cast<std::uint32_t>(std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10));
    }
    if (cp == 0) return 0;
    i = semi + 1;
    return cp;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Pulls attribute value for `name` out of a raw tag body like: a href="x" class='y'.
std::string attribute(std::string_view body, std::string_view name) {
    const std::string low = lower(body);
    std::size_t pos = 0;
    while (true) {
        pos = low.find(std::string(name) + "=", pos);
        if (pos == std::string::npos) return "";
        if (pos > 0 && !std::isspace(static_cast<unsigned char>(low[pos - 1]))) {
            pos += name.size();
            continue;
        }
        std::size_t v = pos + name.size() + 1;
        if (v >= body.size()) return "";
        char quote = body[v];
        if (quote == '"' || quote == '\'') {
            const std::size_t end = body.find(quote, v + 1);
            if (end == std::string_view::npos) return "";
            return std::string(body.substr(v + 1, end - v - 1));
        }
        std::size_t end = body.find_first_of(" \t\n", v);
        return std::string(body.substr(v, end == std::string_view::npos ? std::string_view::npos : end - v));
    }
}

struct HtmlFrame {
    std::string name;
    std::int32_t start = 0;
    bool absorbed = false;  // <code> inside <pre> produces no entity of its own
    TextEntity::Kind kind = TextEntity::Kind::Bold;
    bool emits = false;
    std::string url;
    std::string language;
    std::int64_t user_id = 0;
};

}  // namespace

std::size_t utf16_length(std::string_view utf8) {
    std::size_t units = 0;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if ((c & 0xC0) == 0x80) continue;      // continuation byte
        units += (c >= 0xF0) ? 2 : 1;          // 4-byte lead counts as a surrogate pair
    }
    return units;
}

FormattedText parse_html(std::string_view input) {
    Builder b;
    std::vector<HtmlFrame> stack;
    std::size_t i = 0;
    while (i < input.size()) {
        const char c = input[i];
        if (c == '<') {
            const std::size_t close = input.find('>', i);
            if (close == std::string_view::npos) {
                b.add_char(c);
                ++i;
                continue;
            }
            std::string_view body = input.substr(i + 1, close - i - 1);
            i = close + 1;
            const bool closing = !body.empty() && body[0] == '/';
            if (closing) body.remove_prefix(1);
            while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) body.remove_suffix(1);
            const std::size_t name_end = body.find_first_of(" \t\n/");
            const std::string name = lower(body.substr(0, name_end));
            const std::string_view attrs = name_end == std::string_view::npos ? std::string_view{} : body.substr(name_end);

            if (name == "br" && !closing) {
                b.add_char('\n');
                continue;
            }
            if (closing) {
                for (std::size_t k = stack.size(); k-- > 0;) {
                    if (stack[k].name != name) continue;
                    HtmlFrame frame = std::move(stack[k]);
                    stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(k));
                    if (frame.emits) b.push_entity(frame.kind, frame.start, frame.url, frame.language, frame.user_id);
                    break;
                }
                continue;
            }

            HtmlFrame frame;
            frame.name = name;
            frame.start = b.units;
            if (name == "b" || name == "strong") { frame.kind = TextEntity::Kind::Bold; frame.emits = true; }
            else if (name == "i" || name == "em") { frame.kind = TextEntity::Kind::Italic; frame.emits = true; }
            else if (name == "u" || name == "ins") { frame.kind = TextEntity::Kind::Underline; frame.emits = true; }
            else if (name == "s" || name == "strike" || name == "del") { frame.kind = TextEntity::Kind::Strike; frame.emits = true; }
            else if (name == "tg-spoiler") { frame.kind = TextEntity::Kind::Spoiler; frame.emits = true; }
            else if (name == "blockquote") { frame.kind = TextEntity::Kind::Blockquote; frame.emits = true; }
            else if (name == "span" && lower(attribute(attrs, "class")).find("tg-spoiler") != std::string::npos) {
                frame.kind = TextEntity::Kind::Spoiler;
                frame.emits = true;
            } else if (name == "pre") {
                frame.kind = TextEntity::Kind::Pre;
                frame.emits = true;
            } else if (name == "code") {
                const bool inside_pre = std::any_of(stack.begin(), stack.end(), [](const HtmlFrame& f) { return f.name == "pre"; });
                if (inside_pre) {
                    frame.absorbed = true;
                    const std::string cls = lower(attribute(attrs, "class"));
                    if (cls.rfind("language-", 0) == 0) {
                        for (auto& f : stack) {
                            if (f.name == "pre") f.language = cls.substr(9);
                        }
                    }
                } else {
                    frame.kind = TextEntity::Kind::Code;
                    frame.emits = true;
                }
            } else if (name == "a") {
                const std::string href = attribute(attrs, "href");
                if (!href.empty()) {
                    frame.emits = true;
                    frame.kind = TextEntity::Kind::TextUrl;
                    frame.url = href;
                    const std::string prefix = "tg://user?id=";
                    if (href.rfind(prefix, 0) == 0) {
                        frame.kind = TextEntity::Kind::MentionName;
                        frame.user_id = std::strtoll(href.c_str() + prefix.size(), nullptr, 10);
                        frame.url.clear();
                    }
                }
            } else {
                continue;  // unknown tag: drop the markup, keep the text
            }
            stack.push_back(std::move(frame));
            continue;
        }
        if (c == '&') {
            std::size_t j = i;
            const std::uint32_t cp = decode_entity(input, j);
            if (cp != 0) {
                b.add_codepoint(cp);
                i = j;
                continue;
            }
        }
        b.add_char(c);
        ++i;
    }
    // Close anything left open at the end of the input.
    while (!stack.empty()) {
        HtmlFrame frame = std::move(stack.back());
        stack.pop_back();
        if (frame.emits) b.push_entity(frame.kind, frame.start, frame.url, frame.language, frame.user_id);
    }
    return FormattedText{std::move(b.text), std::move(b.entities)};
}

namespace {

struct MdFrame {
    TextEntity::Kind kind;
    std::int32_t start;
    std::string marker;  // "*", "_", "__", "~", "||", "`", "["
};

bool is_escapable(char c) {
    return std::string_view("_*[]()~`>#+-=|{}.!\\").find(c) != std::string_view::npos;
}

}  // namespace

FormattedText parse_markdown(std::string_view input) {
    Builder b;
    std::vector<MdFrame> stack;
    bool line_start = true;
    std::size_t i = 0;

    auto close_frame = [&](std::size_t idx, const std::string& url = {}) {
        MdFrame f = stack[idx];
        stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(idx));
        if (f.marker == "[") {
            b.push_entity(TextEntity::Kind::TextUrl, f.start, url);
        } else {
            b.push_entity(f.kind, f.start);
        }
    };
    auto find_open = [&](const std::string& marker) -> int {
        for (std::size_t k = stack.size(); k-- > 0;) {
            if (stack[k].marker == marker) return static_cast<int>(k);
        }
        return -1;
    };
    auto toggle = [&](const std::string& marker, TextEntity::Kind kind) {
        const int idx = find_open(marker);
        if (idx >= 0) {
            close_frame(static_cast<std::size_t>(idx));
        } else {
            stack.push_back(MdFrame{kind, b.units, marker});
        }
    };

    while (i < input.size()) {
        const char c = input[i];

        if (c == '\\' && i + 1 < input.size() && is_escapable(input[i + 1])) {
            b.add_char(input[i + 1]);
            i += 2;
            line_start = false;
            continue;
        }

        if (line_start && c == '>') {
            // Blockquote: from here to the end of the line.
            std::size_t eol = input.find('\n', i);
            if (eol == std::string_view::npos) eol = input.size();
            std::size_t body = i + 1;
            if (body < eol && input[body] == ' ') ++body;
            const std::int32_t start = b.units;
            FormattedText inner = parse_markdown(input.substr(body, eol - body));
            // Re-base the inner entities onto this text.
            b.add_bytes(inner.text);
            for (auto& e : inner.entities) {
                e.offset += start;
                b.entities.push_back(e);
            }
            b.push_entity(TextEntity::Kind::Blockquote, start);
            i = eol;
            line_start = false;
            continue;
        }

        if (c == '`' && input.substr(i, 3) == "```") {
            const std::size_t lang_end = input.find('\n', i + 3);
            const std::size_t end = input.find("```", i + 3);
            if (end != std::string_view::npos) {
                std::string language;
                std::size_t body = i + 3;
                if (lang_end != std::string_view::npos && lang_end < end) {
                    language = std::string(input.substr(i + 3, lang_end - i - 3));
                    body = lang_end + 1;
                }
                const std::int32_t start = b.units;
                b.add_bytes(input.substr(body, end - body));
                TextEntity e;
                e.kind = TextEntity::Kind::Pre;
                e.offset = start;
                e.length = b.units - start;
                e.language = language;
                if (e.length > 0) b.entities.push_back(e);
                i = end + 3;
                line_start = false;
                continue;
            }
        }
        if (c == '`') {
            const std::size_t end = input.find('`', i + 1);
            if (end != std::string_view::npos) {
                const std::int32_t start = b.units;
                b.add_bytes(input.substr(i + 1, end - i - 1));
                b.push_entity(TextEntity::Kind::Code, start);
                i = end + 1;
                line_start = false;
                continue;
            }
        }
        if (c == '|' && input.substr(i, 2) == "||") {
            toggle("||", TextEntity::Kind::Spoiler);
            i += 2;
            line_start = false;
            continue;
        }
        if (c == '_' && input.substr(i, 2) == "__") {
            toggle("__", TextEntity::Kind::Underline);
            i += 2;
            line_start = false;
            continue;
        }
        if (c == '*') { toggle("*", TextEntity::Kind::Bold); ++i; line_start = false; continue; }
        if (c == '_') { toggle("_", TextEntity::Kind::Italic); ++i; line_start = false; continue; }
        if (c == '~') { toggle("~", TextEntity::Kind::Strike); ++i; line_start = false; continue; }
        if (c == '[') {
            stack.push_back(MdFrame{TextEntity::Kind::TextUrl, b.units, "["});
            ++i;
            line_start = false;
            continue;
        }
        if (c == ']' && i + 1 < input.size() && input[i + 1] == '(') {
            const int idx = find_open("[");
            const std::size_t end = input.find(')', i + 2);
            if (idx >= 0 && end != std::string_view::npos) {
                const std::string url(input.substr(i + 2, end - i - 2));
                close_frame(static_cast<std::size_t>(idx), url);
                i = end + 1;
                line_start = false;
                continue;
            }
        }

        if (c == '\n') {
            b.add_char('\n');
            ++i;
            line_start = true;
            continue;
        }
        b.add_char(c);
        ++i;
        line_start = false;
    }
    while (!stack.empty()) {
        if (stack.back().marker == "[") {
            stack.pop_back();
        } else {
            close_frame(stack.size() - 1);
        }
    }
    return FormattedText{std::move(b.text), std::move(b.entities)};
}

FormattedText parse_text(std::string_view input, ParseMode mode) {
    switch (mode) {
        case ParseMode::Html: return parse_html(input);
        case ParseMode::Markdown: return parse_markdown(input);
        case ParseMode::None: break;
    }
    FormattedText plain;
    plain.text = std::string(input);
    return plain;
}

}  // namespace cppgram
