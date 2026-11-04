#include <gtest/gtest.h>

#include "cppgram/parser.h"

using cppgram::TextEntity;
using cppgram::parse_html;
using cppgram::parse_markdown;

TEST(ParserHtml, BoldAndItalicWithEmojiUsesUtf16Offsets) {
    const auto r = parse_html("<b>Hi \xF0\x9F\x98\x80</b> <i>there</i>");
    EXPECT_EQ(r.text, "Hi \xF0\x9F\x98\x80 there");
    ASSERT_EQ(r.entities.size(), 2u);
    EXPECT_EQ(r.entities[0].kind, TextEntity::Kind::Bold);
    EXPECT_EQ(r.entities[0].offset, 0);
    EXPECT_EQ(r.entities[0].length, 5);  // "Hi " (3) + emoji (2 UTF-16 units)
    EXPECT_EQ(r.entities[1].kind, TextEntity::Kind::Italic);
    EXPECT_EQ(r.entities[1].offset, 6);
    EXPECT_EQ(r.entities[1].length, 5);
}

TEST(ParserHtml, LinkMentionAndEntities) {
    const auto r = parse_html("<a href=\"https://x.io\">site</a> &amp; <a href=\"tg://user?id=99\">me</a>");
    EXPECT_EQ(r.text, "site & me");
    ASSERT_EQ(r.entities.size(), 2u);
    EXPECT_EQ(r.entities[0].kind, TextEntity::Kind::TextUrl);
    EXPECT_EQ(r.entities[0].url, "https://x.io");
    EXPECT_EQ(r.entities[1].kind, TextEntity::Kind::MentionName);
    EXPECT_EQ(r.entities[1].user_id, 99);
    EXPECT_EQ(r.entities[1].offset, 7);
}

TEST(ParserHtml, PreWithLanguageAbsorbsCode) {
    const auto r = parse_html("<pre><code class=\"language-cpp\">int x;</code></pre>");
    EXPECT_EQ(r.text, "int x;");
    ASSERT_EQ(r.entities.size(), 1u);
    EXPECT_EQ(r.entities[0].kind, TextEntity::Kind::Pre);
    EXPECT_EQ(r.entities[0].language, "cpp");
    EXPECT_EQ(r.entities[0].length, 6);
}

TEST(ParserHtml, UnknownTagsKeepTextAndUnclosedTagsClose) {
    const auto r = parse_html("<div>a<b>b");
    EXPECT_EQ(r.text, "ab");
    ASSERT_EQ(r.entities.size(), 1u);
    EXPECT_EQ(r.entities[0].offset, 1);
    EXPECT_EQ(r.entities[0].length, 1);
}

TEST(ParserMarkdown, BoldItalicCodeAndUnderline) {
    const auto r = parse_markdown("*bold* _it_ `c` __u__");
    EXPECT_EQ(r.text, "bold it c u");
    ASSERT_EQ(r.entities.size(), 4u);
    EXPECT_EQ(r.entities[0].kind, TextEntity::Kind::Bold);
    EXPECT_EQ(r.entities[0].offset, 0);
    EXPECT_EQ(r.entities[0].length, 4);
    EXPECT_EQ(r.entities[1].kind, TextEntity::Kind::Italic);
    EXPECT_EQ(r.entities[1].offset, 5);
    EXPECT_EQ(r.entities[2].kind, TextEntity::Kind::Code);
    EXPECT_EQ(r.entities[2].offset, 8);
    EXPECT_EQ(r.entities[3].kind, TextEntity::Kind::Underline);
    EXPECT_EQ(r.entities[3].offset, 10);
}

TEST(ParserMarkdown, LinkFencedBlockAndEscapes) {
    const auto r = parse_markdown("[go](https://a.b) \\*not bold\\* ```py\nprint(1)```");
    EXPECT_EQ(r.text, "go *not bold* print(1)");
    bool has_link = false, has_pre = false;
    for (const auto& e : r.entities) {
        if (e.kind == TextEntity::Kind::TextUrl) {
            has_link = true;
            EXPECT_EQ(e.url, "https://a.b");
            EXPECT_EQ(e.length, 2);
        }
        if (e.kind == TextEntity::Kind::Pre) {
            has_pre = true;
            EXPECT_EQ(e.language, "py");
        }
    }
    EXPECT_TRUE(has_link);
    EXPECT_TRUE(has_pre);
}

TEST(ParserMarkdown, SnakeCaseStillPlainWhenUnclosed) {
    const auto r = parse_markdown("snake_case");
    EXPECT_EQ(r.text, "snakecase");  // unmatched '_' opens italic until end of input
    ASSERT_EQ(r.entities.size(), 1u);
    EXPECT_EQ(r.entities[0].kind, TextEntity::Kind::Italic);
}

TEST(ParserHtml, NestedBoldItalicCoversInnerRange) {
    const auto r = parse_html("<b>a<i>b</i>c</b>");
    EXPECT_EQ(r.text, "abc");
    ASSERT_EQ(r.entities.size(), 2u);
    // Entities are emitted when their tag closes: the inner italic first, then the outer bold.
    const auto& italic = r.entities[0];
    const auto& bold = r.entities[1];
    EXPECT_EQ(italic.kind, TextEntity::Kind::Italic);
    EXPECT_EQ(italic.offset, 1);
    EXPECT_EQ(italic.length, 1);
    EXPECT_EQ(bold.kind, TextEntity::Kind::Bold);
    EXPECT_EQ(bold.offset, 0);
    EXPECT_EQ(bold.length, 3);
}

TEST(ParserHtml, NestedLinkInsideBoldKeepsUrl) {
    const auto r = parse_html("<b>go <a href=\"https://example.com\">here</a></b>");
    EXPECT_EQ(r.text, "go here");
    bool found_link = false;
    for (const auto& e : r.entities) {
        if (e.kind == TextEntity::Kind::TextUrl) {
            found_link = true;
            EXPECT_EQ(e.offset, 3);
            EXPECT_EQ(e.length, 4);
            EXPECT_EQ(e.url, "https://example.com");
        }
    }
    EXPECT_TRUE(found_link);
}
