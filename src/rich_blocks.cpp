// Rich result page blocks, decoded into RichBlockInfo. Every pageBlock constructor in layer 233 is mapped to a
// RichBlockKind. Fields without a public member stay in RichBlockInfo::raw.
#include "rich_blocks.h"

#include <type_traits>

#include "cppgram/errors.h"

namespace cppgram::detail {
namespace {

namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

constexpr int kMaxDepth = 32;

[[noreturn]] void too_deep() { throw errors::SerializationError("rich result blocks are nested too deeply"); }

std::string plain(const std::shared_ptr<tl::RichText>& text, int depth);

// Every RichText kind that wraps other text (bold, url, mention, date and so on) has a text member. A kind without one
// (textEmpty, textImage, textMath, and similar) contributes no plain text. Its full object stays in the block's raw.
std::string plain_of(const tl::RichText& t, int depth) {
    if (depth > kMaxDepth) too_deep();
    return std::visit(
        [depth](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, ty::textPlain>) {
                return x.text;
            } else if constexpr (std::is_same_v<T, ty::textConcat>) {
                std::string out;
                for (const auto& part : x.texts) out += plain(part, depth + 1);
                return out;
            } else if constexpr (requires { x.text; }) {
                if constexpr (std::is_same_v<std::decay_t<decltype(x.text)>, std::shared_ptr<tl::RichText>>) {
                    return plain(x.text, depth + 1);
                } else {
                    return std::string();
                }
            } else {
                return std::string();
            }
        },
        t.value);
}

std::string plain(const std::shared_ptr<tl::RichText>& text, int depth) {
    if (!text) return std::string();
    return plain_of(*text, depth);
}

std::string caption_text(const std::shared_ptr<tl::PageCaption>& caption) {
    if (!caption) return std::string();
    const auto* c = std::get_if<ty::pageCaption>(&caption->value);
    return c ? plain(c->text, 0) : std::string();
}

RichBlockInfo block(const std::shared_ptr<tl::PageBlock>& p, int depth);

std::vector<RichBlockInfo> blocks_of(const std::vector<std::shared_ptr<tl::PageBlock>>& list, int depth) {
    std::vector<RichBlockInfo> out;
    out.reserve(list.size());
    for (const auto& b : list) out.push_back(block(b, depth + 1));
    return out;
}

RichBlockInfo list_item(const std::shared_ptr<tl::PageListItem>& p, int depth) {
    if (!p) throw errors::SerializationError("empty rich list item");
    RichBlockInfo out;
    out.kind = RichBlockKind::ListItem;
    out.constructor_id = std::visit([](const auto& v) { return std::decay_t<decltype(v)>::kId; }, p->value);
    if (const auto* t = std::get_if<ty::pageListItemText>(&p->value)) {
        out.checkbox = t->checkbox;
        out.checked = t->checked;
        out.text = plain(t->text, depth + 1);
    } else if (const auto* b = std::get_if<ty::pageListItemBlocks>(&p->value)) {
        out.checkbox = b->checkbox;
        out.checked = b->checked;
        out.items = blocks_of(b->blocks, depth + 1);
    } else {
        throw errors::SerializationError("unsupported rich list item");
    }
    return out;
}

RichBlockInfo ordered_item(const std::shared_ptr<tl::PageListOrderedItem>& p, int depth) {
    if (!p) throw errors::SerializationError("empty rich ordered list item");
    RichBlockInfo out;
    out.kind = RichBlockKind::ListItem;
    out.constructor_id = std::visit([](const auto& v) { return std::decay_t<decltype(v)>::kId; }, p->value);
    if (const auto* t = std::get_if<ty::pageListOrderedItemText>(&p->value)) {
        out.checkbox = t->checkbox;
        out.checked = t->checked;
        if (t->num) out.label = *t->num;
        out.text = plain(t->text, depth + 1);
    } else if (const auto* b = std::get_if<ty::pageListOrderedItemBlocks>(&p->value)) {
        out.checkbox = b->checkbox;
        out.checked = b->checked;
        if (b->num) out.label = *b->num;
        out.items = blocks_of(b->blocks, depth + 1);
    } else {
        throw errors::SerializationError("unsupported rich ordered list item");
    }
    return out;
}

RichBlockInfo block(const std::shared_ptr<tl::PageBlock>& p, int depth) {
    if (depth > kMaxDepth) too_deep();
    if (!p) throw errors::SerializationError("empty rich page block");
    RichBlockInfo out;
    out.raw = p;
    out.constructor_id = std::visit([](const auto& v) { return std::decay_t<decltype(v)>::kId; }, p->value);
    const auto& v = p->value;
    if (std::holds_alternative<ty::pageBlockUnsupported>(v)) {
        out.kind = RichBlockKind::Unsupported;
    } else if (const auto* x = std::get_if<ty::pageBlockTitle>(&v)) {
        out.kind = RichBlockKind::Title;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockSubtitle>(&v)) {
        out.kind = RichBlockKind::Subtitle;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockAuthorDate>(&v)) {
        out.kind = RichBlockKind::AuthorDate;
        out.text = plain(x->author, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockHeader>(&v)) {
        out.kind = RichBlockKind::Header;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockSubheader>(&v)) {
        out.kind = RichBlockKind::Subheader;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockParagraph>(&v)) {
        out.kind = RichBlockKind::Paragraph;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockPreformatted>(&v)) {
        out.kind = RichBlockKind::Preformatted;
        out.text = plain(x->text, 0);
        out.language = x->language;
    } else if (const auto* x = std::get_if<ty::pageBlockFooter>(&v)) {
        out.kind = RichBlockKind::Footer;
        out.text = plain(x->text, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockKicker>(&v)) {
        out.kind = RichBlockKind::Kicker;
        out.text = plain(x->text, 0);
    } else if (std::holds_alternative<ty::pageBlockDivider>(v)) {
        out.kind = RichBlockKind::Divider;
    } else if (const auto* x = std::get_if<ty::pageBlockAnchor>(&v)) {
        out.kind = RichBlockKind::Anchor;
        out.text = x->name;
    } else if (const auto* x = std::get_if<ty::pageBlockList>(&v)) {
        out.kind = RichBlockKind::List;
        for (const auto& item : x->items) out.items.push_back(list_item(item, depth + 1));
    } else if (const auto* x = std::get_if<ty::pageBlockOrderedList>(&v)) {
        out.kind = RichBlockKind::OrderedList;
        for (const auto& item : x->items) out.items.push_back(ordered_item(item, depth + 1));
    } else if (const auto* x = std::get_if<ty::pageBlockBlockquote>(&v)) {
        out.kind = RichBlockKind::Blockquote;
        out.text = plain(x->text, 0);
        out.caption = plain(x->caption, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockBlockquoteBlocks>(&v)) {
        out.kind = RichBlockKind::BlockquoteBlocks;
        out.caption = plain(x->caption, 0);
        out.items = blocks_of(x->blocks, depth);
    } else if (const auto* x = std::get_if<ty::pageBlockPullquote>(&v)) {
        out.kind = RichBlockKind::Pullquote;
        out.text = plain(x->text, 0);
        out.caption = plain(x->caption, 0);
    } else if (const auto* x = std::get_if<ty::pageBlockPhoto>(&v)) {
        out.kind = RichBlockKind::Photo;
        out.media_id = x->photo_id;
        out.caption = caption_text(x->caption);
        if (x->url) out.url = *x->url;
    } else if (const auto* x = std::get_if<ty::pageBlockVideo>(&v)) {
        out.kind = RichBlockKind::Video;
        out.media_id = x->video_id;
        out.caption = caption_text(x->caption);
    } else if (const auto* x = std::get_if<ty::pageBlockAudio>(&v)) {
        out.kind = RichBlockKind::Audio;
        out.media_id = x->audio_id;
        out.caption = caption_text(x->caption);
    } else if (const auto* x = std::get_if<ty::pageBlockDocument>(&v)) {
        out.kind = RichBlockKind::Document;
        out.media_id = x->document_id;
        out.caption = caption_text(x->caption);
    } else if (const auto* x = std::get_if<ty::pageBlockCover>(&v)) {
        out.kind = RichBlockKind::Cover;
        out.items.push_back(block(x->cover, depth + 1));
    } else if (const auto* x = std::get_if<ty::pageBlockEmbed>(&v)) {
        out.kind = RichBlockKind::Embed;
        if (x->url) out.url = *x->url;
        out.caption = caption_text(x->caption);
    } else if (const auto* x = std::get_if<ty::pageBlockEmbedPost>(&v)) {
        out.kind = RichBlockKind::EmbedPost;
        out.url = x->url;
        out.text = x->author;
        out.caption = caption_text(x->caption);
        out.items = blocks_of(x->blocks, depth);
    } else if (const auto* x = std::get_if<ty::pageBlockCollage>(&v)) {
        out.kind = RichBlockKind::Collage;
        out.caption = caption_text(x->caption);
        out.items = blocks_of(x->items, depth);
    } else if (const auto* x = std::get_if<ty::pageBlockSlideshow>(&v)) {
        out.kind = RichBlockKind::Slideshow;
        out.caption = caption_text(x->caption);
        out.items = blocks_of(x->items, depth);
    } else if (const auto* x = std::get_if<ty::pageBlockChannel>(&v)) {
        out.kind = RichBlockKind::Channel;
        if (x->channel) {
            if (const auto* ch = std::get_if<ty::channel>(&x->channel->value)) {
                out.channel_id = ch->id;
                out.channel_title = ch->title;
                if (ch->username) out.channel_username = *ch->username;
            } else if (const auto* ct = std::get_if<ty::chat>(&x->channel->value)) {
                out.channel_id = ct->id;
                out.channel_title = ct->title;
            } else if (const auto* cf = std::get_if<ty::chatForbidden>(&x->channel->value)) {
                out.channel_id = cf->id;
                out.channel_title = cf->title;
            } else if (const auto* chf = std::get_if<ty::channelForbidden>(&x->channel->value)) {
                out.channel_id = chf->id;  // Telegram sends no title for a forbidden channel
            }
        }
    } else if (const auto* x = std::get_if<ty::pageBlockTable>(&v)) {
        out.kind = RichBlockKind::Table;
        out.text = plain(x->title, 0);
        out.bordered = x->bordered;
        out.striped = x->striped;
        out.compact = x->compact;
        for (const auto& row : x->rows) {
            const auto* r = row ? std::get_if<ty::pageTableRow>(&row->value) : nullptr;
            if (r == nullptr) throw errors::SerializationError("malformed table row in a rich result");
            std::vector<RichTableCell> cells;
            cells.reserve(r->cells.size());
            for (const auto& cell : r->cells) {
                const auto* c = cell ? std::get_if<ty::pageTableCell>(&cell->value) : nullptr;
                if (c == nullptr) throw errors::SerializationError("malformed table cell in a rich result");
                RichTableCell rc;
                if (c->text && *c->text) rc.text = plain(*c->text, 0);
                rc.header = c->header;
                rc.align_center = c->align_center;
                rc.align_right = c->align_right;
                rc.valign_middle = c->valign_middle;
                rc.valign_bottom = c->valign_bottom;
                rc.colspan = c->colspan.value_or(1);
                rc.rowspan = c->rowspan.value_or(1);
                if (rc.colspan < 1 || rc.rowspan < 1) {
                    throw errors::SerializationError("table cell span must be at least 1 in a rich result");
                }
                cells.push_back(std::move(rc));
            }
            out.rows.push_back(std::move(cells));
        }
    } else if (const auto* x = std::get_if<ty::pageBlockDetails>(&v)) {
        out.kind = RichBlockKind::Details;
        out.text = plain(x->title, 0);
        out.items = blocks_of(x->blocks, depth);
    } else if (const auto* x = std::get_if<ty::pageBlockRelatedArticles>(&v)) {
        out.kind = RichBlockKind::RelatedArticles;
        out.text = plain(x->title, 0);
        for (const auto& article : x->articles) {
            const auto* a = article ? std::get_if<ty::pageRelatedArticle>(&article->value) : nullptr;
            if (a == nullptr) throw errors::SerializationError("malformed related article in a rich result");
            RichRelatedArticle item;
            item.url = a->url;
            item.webpage_id = a->webpage_id;
            item.title = a->title.value_or(std::string());
            item.description = a->description.value_or(std::string());
            item.author = a->author.value_or(std::string());
            item.photo_id = a->photo_id;
            item.published_date = a->published_date;
            out.articles.push_back(std::move(item));
        }
    } else if (const auto* x = std::get_if<ty::pageBlockMap>(&v)) {
        out.kind = RichBlockKind::Map;
        out.caption = caption_text(x->caption);
        out.zoom = x->zoom;
        out.width = x->w;
        out.height = x->h;
        if (x->geo) {
            if (const auto* g = std::get_if<ty::geoPoint>(&x->geo->value)) {
                out.has_geo = true;
                out.latitude = g->lat;
                out.longitude = g->long_;
            }
        }
    } else if (const auto* x = std::get_if<ty::pageBlockMath>(&v)) {
        out.kind = RichBlockKind::Math;
        out.text = x->source;
    } else if (const auto* x = std::get_if<ty::pageBlockThinking>(&v)) {
        out.kind = RichBlockKind::Thinking;
        out.text = plain(x->text, 0);
    } else if (std::holds_alternative<ty::pageBlockButtonRow>(v)) {
        out.kind = RichBlockKind::ButtonRow;
    } else {
        throw errors::SerializationError("unsupported rich page block");
    }
    return out;
}

}  // namespace

std::vector<RichBlockInfo> rich_blocks_of(const std::vector<std::shared_ptr<tl::PageBlock>>& blocks) {
    return blocks_of(blocks, 0);
}

std::string rich_text_plain(const std::shared_ptr<tl::RichText>& text) { return plain(text, 0); }

}  // namespace cppgram::detail
