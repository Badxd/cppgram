#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cppgram {

enum class ParseMode { None, Html, Markdown };

struct TextEntity {
    enum class Kind { Bold, Italic, Underline, Strike, Spoiler, Code, Pre, TextUrl, MentionName, Blockquote };
    Kind kind = Kind::Bold;
    std::int32_t offset = 0;  // UTF-16 code units, as Telegram counts them
    std::int32_t length = 0;  // UTF-16 code units
    std::string url;          // TextUrl
    std::string language;     // Pre
    std::int64_t user_id = 0; // MentionName
};

struct FormattedText {
    std::string text;  // plain UTF-8 text with markup removed
    std::vector<TextEntity> entities;
};

// Parses the HTML subset Telegram accepts: b/strong, i/em, u/ins, s/strike/del, code, pre (with
// language-x class), a href (links and tg://user?id= mentions), span.tg-spoiler / tg-spoiler,
// blockquote, br, and the usual character entities. Unknown tags are dropped, their text kept.
FormattedText parse_html(std::string_view input);

// Parses MarkdownV2: *bold*, _italic_, __underline__, ~strike~, ||spoiler||, `code`,
// ```lang fenced blocks```, [text](url), >quote at line start, and backslash escapes.
FormattedText parse_markdown(std::string_view input);

FormattedText parse_text(std::string_view input, ParseMode mode);

// Telegram offsets/lengths are UTF-16 code units.
std::size_t utf16_length(std::string_view utf8);

}  // namespace cppgram
