// messages.* calls not covered before, plus stickers, Web Apps, ephemeral messages, birthday and top peers.
#include "cppgram/client.h"
#include "secure_random.h"
#include "markup_detail.h"
#include "rich_blocks.h"
#include "cppgram/errors.h"

#include <stdexcept>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

// Media of a photo result. Empty photos give nullopt.
std::optional<MediaInfo> photo_media_of(const std::shared_ptr<tl::Photo>& photo) {
    if (!photo) return std::nullopt;
    const auto* p = std::get_if<ty::photo>(&photo->value);
    if (p == nullptr) return std::nullopt;
    MediaInfo m;
    m.kind = MediaKind::Photo;
    m.id = p->id;
    m.access_hash = p->access_hash;
    m.file_reference = p->file_reference;
    m.dc_id = p->dc_id;
    return m;
}

std::vector<TextEntity> entities_of(const std::optional<std::vector<std::shared_ptr<tl::MessageEntity>>>& list) {
    std::vector<TextEntity> out;
    if (!list) return out;
    for (const auto& e : *list) {
        if (!e) continue;
        if (auto v = detail::text_entity_from_tl(*e)) out.push_back(*v);
    }
    return out;
}

// Web documents (thumbnails and contents of inline results). Attributes this client knows are decoded. Any other
// attribute throws, so nothing is dropped silently.
void apply_web_attribute(WebDocumentInfo& out, const tl::DocumentAttribute& a) {
    if (const auto* size = std::get_if<ty::documentAttributeImageSize>(&a.value)) {
        out.width = size->w;
        out.height = size->h;
    } else if (const auto* video = std::get_if<ty::documentAttributeVideo>(&a.value)) {
        out.duration = video->duration;
        out.width = video->w;
        out.height = video->h;
    } else if (const auto* audio = std::get_if<ty::documentAttributeAudio>(&a.value)) {
        out.duration = audio->duration;
    } else if (const auto* name = std::get_if<ty::documentAttributeFilename>(&a.value)) {
        out.file_name = name->file_name;
    } else {
        // Not decoded. Recorded by constructor id so the caller can see it, and the result is not failed.
        out.unknown_attribute_ids.push_back(std::visit([](const auto& x) { return std::decay_t<decltype(x)>::kId; }, a.value));
    }
}

WebDocumentInfo web_document_of(const tl::WebDocument& w) {
    WebDocumentInfo out;
    const std::vector<std::shared_ptr<tl::DocumentAttribute>>* attributes = nullptr;
    if (const auto* d = std::get_if<ty::webDocument>(&w.value)) {
        out.url = d->url;
        out.access_hash = d->access_hash;
        out.size = d->size;
        out.mime_type = d->mime_type;
        attributes = &d->attributes;
    } else if (const auto* np = std::get_if<ty::webDocumentNoProxy>(&w.value)) {
        out.url = np->url;
        out.size = np->size;
        out.mime_type = np->mime_type;
        attributes = &np->attributes;
    } else {
        throw errors::SerializationError("unsupported web document variant");
    }
    for (const auto& a : *attributes) {
        if (!a) throw errors::SerializationError("empty web document attribute");
        apply_web_attribute(out, *a);
    }
    return out;
}

// Decodes a bot inline result. botInlineResult and botInlineMediaResult share the same send_message variants.
InlineResultInfo inline_result_of(const tl::BotInlineResult& r) {
    InlineResultInfo out;
    const tl::BotInlineMessage* message = nullptr;
    if (const auto* plain = std::get_if<ty::botInlineResult>(&r.value)) {
        out.id = plain->id;
        out.type = plain->type;
        if (plain->title) out.title = *plain->title;
        if (plain->description) out.description = *plain->description;
        if (plain->url) out.url = *plain->url;
        if (plain->thumb && *plain->thumb) out.thumb = web_document_of(**plain->thumb);
        if (plain->content && *plain->content) out.content = web_document_of(**plain->content);
        if (plain->send_message) message = plain->send_message.get();
    } else if (const auto* media = std::get_if<ty::botInlineMediaResult>(&r.value)) {
        out.id = media->id;
        out.type = media->type;
        if (media->title) out.title = *media->title;
        if (media->description) out.description = *media->description;
        if (media->photo && *media->photo) out.media = photo_media_of(*media->photo);
        if (media->document && *media->document) {
            if (auto m = detail::media_info_from_document(**media->document)) out.media = *m;
        }
        if (media->send_message) message = media->send_message.get();
    } else {
        throw errors::SerializationError("unsupported botInlineResult variant");
    }
    if (message == nullptr) throw errors::SerializationError("inline result has no message");
    auto markup_of = [&out](const std::optional<std::shared_ptr<tl::ReplyMarkup>>& m) {
        if (!m || !*m) return;
        if (auto inl = detail::from_tl_inline_markup(**m)) {
            out.inline_markup = std::move(*inl);
        } else if (auto rep = detail::from_tl_reply_markup(**m)) {
            out.reply_markup = std::move(*rep);
        }
    };
    if (const auto* t = std::get_if<ty::botInlineMessageText>(&message->value)) {
        out.message_kind = InlineMessageKind::Text;
        out.message_text = t->message;
        out.entities = entities_of(t->entities);
        out.no_webpage = t->no_webpage;
        out.invert_media = t->invert_media;
        markup_of(t->reply_markup);
    } else if (const auto* a = std::get_if<ty::botInlineMessageMediaAuto>(&message->value)) {
        out.message_kind = InlineMessageKind::Text;
        out.message_text = a->message;
        out.entities = entities_of(a->entities);
        out.invert_media = a->invert_media;
        markup_of(a->reply_markup);
    } else if (const auto* w = std::get_if<ty::botInlineMessageMediaWebPage>(&message->value)) {
        out.message_kind = InlineMessageKind::WebPage;
        out.message_text = w->message;
        out.entities = entities_of(w->entities);
        out.web_page_url = w->url;
        out.invert_media = w->invert_media;
        markup_of(w->reply_markup);
    } else if (const auto* g = std::get_if<ty::botInlineMessageMediaGeo>(&message->value)) {
        out.message_kind = InlineMessageKind::Geo;
        if (g->geo) if (const auto* gp = std::get_if<ty::geoPoint>(&g->geo->value)) {
            out.latitude = gp->lat;
            out.longitude = gp->long_;
        }
        markup_of(g->reply_markup);
    } else if (const auto* v = std::get_if<ty::botInlineMessageMediaVenue>(&message->value)) {
        out.message_kind = InlineMessageKind::Venue;
        if (v->geo) if (const auto* gp = std::get_if<ty::geoPoint>(&v->geo->value)) {
            out.latitude = gp->lat;
            out.longitude = gp->long_;
        }
        out.venue_title = v->title;
        out.address = v->address;
        out.provider = v->provider;
        out.venue_id = v->venue_id;
        out.venue_type = v->venue_type;
        markup_of(v->reply_markup);
    } else if (const auto* c = std::get_if<ty::botInlineMessageMediaContact>(&message->value)) {
        out.message_kind = InlineMessageKind::Contact;
        out.phone_number = c->phone_number;
        out.first_name = c->first_name;
        out.last_name = c->last_name;
        out.vcard = c->vcard;
        markup_of(c->reply_markup);
    } else if (const auto* i = std::get_if<ty::botInlineMessageMediaInvoice>(&message->value)) {
        out.message_kind = InlineMessageKind::Invoice;
        out.invoice_title = i->title;
        out.invoice_description = i->description;
        out.currency = i->currency;
        out.total_amount = i->total_amount;
        out.invoice_test = i->test;
        out.shipping_address_requested = i->shipping_address_requested;
        if (i->photo && *i->photo) out.invoice_photo = web_document_of(**i->photo);
        markup_of(i->reply_markup);
    } else if (const auto* rm = std::get_if<ty::botInlineMessageRichMessage>(&message->value)) {
        out.message_kind = InlineMessageKind::Rich;
        markup_of(rm->reply_markup);
        if (!rm->rich_message) throw errors::SerializationError("rich inline result has no rich message");
        out.rich_raw = rm->rich_message;
        const auto* body = std::get_if<ty::richMessage>(&rm->rich_message->value);
        if (body == nullptr) throw errors::SerializationError("unsupported richMessage variant");
        out.rich_rtl = body->rtl;
        out.rich_part = body->part;
        out.rich_block_count = body->blocks.size();
        out.rich_blocks = detail::rich_blocks_of(body->blocks);
        for (const auto& ph : body->photos) {
            if (!ph) continue;
            if (auto m = photo_media_of(ph)) out.rich_photos.push_back(*m);
        }
        for (const auto& dc : body->documents) {
            if (!dc) continue;
            if (auto m = detail::media_info_from_document(*dc)) out.rich_documents.push_back(*m);
        }
    } else if (const auto* wg = std::get_if<ty::botInlineMessageWidget>(&message->value)) {
        out.message_kind = InlineMessageKind::Widget;
        out.message_text = wg->message;
        out.entities = entities_of(wg->entities);
        out.invert_media = wg->invert_media;
        out.widget_width = wg->width;
        out.widget_height = wg->height;
        out.widget_payload.assign(wg->payload.begin(), wg->payload.end());
        out.public_data = wg->public_data;
        out.public_data_hash = wg->public_data_hash;
        out.chat_data = wg->chat_data;
        out.widget_id = wg->widget_id;
        markup_of(wg->reply_markup);
    } else {
        throw errors::SerializationError("unsupported bot inline message variant");
    }
    return out;
}


std::shared_ptr<tl::InputPeer> input_peer_ptr(const Client& client, const PeerRef& peer) {
    return std::make_shared<tl::InputPeer>(client.input_peer(peer));
}

std::shared_ptr<tl::InputDocument> input_document_ptr(const DocumentRef& d) {
    return std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{d.id, d.access_hash, d.file_reference}});
}

std::shared_ptr<tl::InputStickerSet> sticker_set_ptr(const StickerSetRef& set) {
    switch (set.kind) {
        case StickerSetRef::Kind::Id:
            return std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetID{set.id, set.access_hash}});
        case StickerSetRef::Kind::ShortName:
            if (set.short_name.empty()) throw std::invalid_argument("sticker set short name must not be empty");
            return std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetShortName{set.short_name}});
        case StickerSetRef::Kind::Empty:
            return std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetEmpty{}});
    }
    throw std::invalid_argument("unknown sticker set kind");
}

std::shared_ptr<tl::InputPeer> send_as_ptr(const Client& client, const PeerRef& p) {
    return input_peer_ptr(client, p);
}

std::optional<std::shared_ptr<tl::ReplyMarkup>> markup_of_result(const InlineTextResultInfo& r) {
    if (r.inline_markup && r.reply_markup) {
        throw std::invalid_argument("an inline result takes one of inline_markup or reply_markup");
    }
    if (r.inline_markup) return detail::to_tl_inline_markup(*r.inline_markup);
    if (r.reply_markup) return detail::to_tl_reply_markup(*r.reply_markup);
    return std::nullopt;
}

std::shared_ptr<tl::InputRichMessage> rich_message_ptr(const RichMessageInput& rich) {
    if (rich.body.empty()) throw std::invalid_argument("rich message needs a body");
    std::vector<std::shared_ptr<tl::InputRichFile>> files;
    for (const auto& f : rich.files) {
        if (f.id.empty()) throw std::invalid_argument("rich message file needs an id");
        if (f.media.kind == MediaKind::Photo) {
            ty::inputRichFilePhoto file;
            file.id = f.id;
            file.photo = std::make_shared<tl::InputPhoto>(
                tl::InputPhoto{ty::inputPhoto{f.media.id, f.media.access_hash, f.media.file_reference}});
            files.push_back(std::make_shared<tl::InputRichFile>(tl::InputRichFile{file}));
        } else if (f.media.kind == MediaKind::Document) {
            ty::inputRichFileDocument file;
            file.id = f.id;
            file.document = std::make_shared<tl::InputDocument>(
                tl::InputDocument{ty::inputDocument{f.media.id, f.media.access_hash, f.media.file_reference}});
            files.push_back(std::make_shared<tl::InputRichFile>(tl::InputRichFile{file}));
        } else {
            throw std::invalid_argument("rich message files must be cached photos or documents");
        }
    }
    if (rich.format == RichMessageInput::Format::Html) {
        ty::inputRichMessageHTML out;
        out.html = rich.body;
        if (rich.rtl) out.rtl = true;
        if (rich.noautolink) out.noautolink = true;
        if (!files.empty()) out.files = std::move(files);
        return std::make_shared<tl::InputRichMessage>(tl::InputRichMessage{out});
    }
    ty::inputRichMessageMarkdown out;
    out.markdown = rich.body;
    if (rich.rtl) out.rtl = true;
    if (rich.noautolink) out.noautolink = true;
    if (!files.empty()) out.files = std::move(files);
    return std::make_shared<tl::InputRichMessage>(tl::InputRichMessage{out});
}

// Uploads the local files of a rich message body before the body is sent. Each path goes through save_file, then
// messages.uploadMedia with the bot's own peer. The photo or document Telegram returns becomes the file's media. Files
// that already name media are not changed, and a file with both a path and media is refused. call issues the request.
template <typename Call>
RichMessageInput with_uploaded_rich_files(Client& client, const RichMessageInput& in, Call&& call) {
    bool has_path = false;
    for (const auto& f : in.files) has_path = has_path || !f.path.empty();
    if (!has_path) return in;
    RichMessageInput out = in;
    for (auto& f : out.files) {
        if (f.path.empty()) continue;
        if (f.id.empty()) throw std::invalid_argument("rich message file needs an id");
        if (f.media.kind != MediaKind::None) {
            throw std::invalid_argument("rich message file takes a path or cached media, not both");
        }
        UploadedFile uploaded = client.save_file(f.path);
        fn::messages_uploadMedia up;
        up.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
        up.media = detail::uploaded_input_media(uploaded, f.as_photo);
        const tl::MessageMedia stored = call(up);
        if (const auto* mp = std::get_if<ty::messageMediaPhoto>(&stored.value)) {
            const auto media = mp->photo ? photo_media_of(*mp->photo) : std::nullopt;
            if (!media) throw std::runtime_error("uploadMedia returned no photo for a rich message file");
            f.media = *media;
        } else if (const auto* md = std::get_if<ty::messageMediaDocument>(&stored.value)) {
            const auto media = md->document ? detail::media_info_from_document(**md->document) : std::nullopt;
            if (!media) throw std::runtime_error("uploadMedia returned no document for a rich message file");
            f.media = *media;
        } else {
            throw std::runtime_error("uploadMedia returned an unexpected media type for a rich message file");
        }
    }
    return out;
}

// Media sent by ephemeral methods. Only cached photos and documents can be sent; uploads go through the send helpers.
std::shared_ptr<tl::InputMedia> cached_input_media(const MediaInfo& media) {
    if (media.kind == MediaKind::Photo) {
        ty::inputMediaPhoto photo;
        photo.id = std::make_shared<tl::InputPhoto>(
            tl::InputPhoto{ty::inputPhoto{media.id, media.access_hash, media.file_reference}});
        return std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
    }
    if (media.kind == MediaKind::Document) {
        ty::inputMediaDocument doc;
        doc.id = std::make_shared<tl::InputDocument>(
            tl::InputDocument{ty::inputDocument{media.id, media.access_hash, media.file_reference}});
        return std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
    }
    throw std::invalid_argument("ephemeral messages can only send cached photos or documents");
}

// Input message of an inline result, built from the result's kind.
std::shared_ptr<tl::InputBotInlineMessage> inline_message_of(const InlineTextResultInfo& r) {
    auto wrap = [](auto&& msg) {
        return std::make_shared<tl::InputBotInlineMessage>(tl::InputBotInlineMessage{std::move(msg)});
    };
    auto entities_list = [&r]() {
        std::vector<std::shared_ptr<tl::MessageEntity>> list;
        for (const auto& e : r.entities) list.push_back(detail::to_tl_entity(e));
        return list;
    };
    switch (r.kind) {
        case InlineTextResultInfo::Kind::Text: {
            if (r.text.empty()) throw std::invalid_argument("text inline result needs a message");
            ty::inputBotInlineMessageText msg;
            msg.message = r.text;
            if (r.no_webpage) msg.no_webpage = true;
            if (r.invert_media) msg.invert_media = true;
            if (!r.entities.empty()) msg.entities = entities_list();
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::WebPage: {
            if (r.text.empty()) throw std::invalid_argument("web page inline result needs a message");
            if (r.url.empty()) throw std::invalid_argument("web page inline result needs a url");
            ty::inputBotInlineMessageMediaWebPage msg;
            msg.message = r.text;
            msg.url = r.url;
            if (r.invert_media) msg.invert_media = true;
            if (!r.entities.empty()) msg.entities = entities_list();
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Geo: {
            ty::inputBotInlineMessageMediaGeo msg;
            ty::inputGeoPoint g;
            g.lat = r.latitude;
            g.long_ = r.longitude;
            msg.geo_point = std::make_shared<tl::InputGeoPoint>(tl::InputGeoPoint{g});
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Venue: {
            ty::inputBotInlineMessageMediaVenue msg;
            ty::inputGeoPoint g;
            g.lat = r.latitude;
            g.long_ = r.longitude;
            msg.geo_point = std::make_shared<tl::InputGeoPoint>(tl::InputGeoPoint{g});
            msg.title = r.title;
            msg.address = r.address;
            msg.provider = r.provider;
            msg.venue_id = r.venue_id;
            msg.venue_type = r.venue_type;
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Contact: {
            if (r.phone_number.empty()) throw std::invalid_argument("contact inline result needs a phone number");
            ty::inputBotInlineMessageMediaContact msg;
            msg.phone_number = r.phone_number;
            msg.first_name = r.first_name;
            msg.last_name = r.last_name;
            msg.vcard = r.vcard;
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Rich: {
            if (!r.rich_message) throw std::invalid_argument("rich inline result needs a rich message");
            ty::inputBotInlineMessageRichMessage msg;
            msg.rich_message = rich_message_ptr(*r.rich_message);
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Invoice: {
            if (!r.invoice) throw std::invalid_argument("invoice inline result needs an invoice");
            const InvoiceInfo& in = *r.invoice;
            // Limits from the Bot API sendInvoice description: title 1-32, description 1-255, payload 1-128. Sizes are
            // counted in bytes, which is stricter than characters for non-ASCII text.
            if (in.title.empty() || in.title.size() > 32) throw std::invalid_argument("invoice title must be 1 to 32 bytes");
            if (in.description.empty() || in.description.size() > 255) {
                throw std::invalid_argument("invoice description must be 1 to 255 bytes");
            }
            if (in.payload.empty() || in.payload.size() > 128) {
                throw std::invalid_argument("invoice payload must be 1 to 128 bytes");
            }
            if (in.currency.size() != 3) throw std::invalid_argument("invoice currency must be a 3-letter ISO 4217 code");
            if (in.prices.empty()) throw std::invalid_argument("invoice needs at least one price");
            if (in.provider.empty()) throw std::invalid_argument("invoice needs a payment provider");
            ty::invoice inv;
            inv.test = in.test;
            inv.name_requested = in.name_requested;
            inv.phone_requested = in.phone_requested;
            inv.email_requested = in.email_requested;
            inv.shipping_address_requested = in.shipping_address_requested;
            inv.flexible = in.flexible;
            inv.phone_to_provider = in.phone_to_provider;
            inv.email_to_provider = in.email_to_provider;
            inv.recurring = in.recurring;
            inv.currency = in.currency;
            for (const auto& p : in.prices) {
                if (p.label.empty()) throw std::invalid_argument("invoice price needs a label");
                ty::labeledPrice lp;
                lp.label = p.label;
                lp.amount = p.amount;
                inv.prices.push_back(std::make_shared<tl::LabeledPrice>(tl::LabeledPrice{lp}));
            }
            inv.max_tip_amount = in.max_tip_amount;
            inv.suggested_tip_amounts = in.suggested_tip_amounts;
            inv.terms_url = in.terms_url;
            inv.subscription_period = in.subscription_period;
            ty::inputBotInlineMessageMediaInvoice msg;
            msg.title = in.title;
            msg.description = in.description;
            msg.invoice = std::make_shared<tl::Invoice>(tl::Invoice{inv});
            msg.payload = mtproto::Bytes(in.payload.begin(), in.payload.end());
            msg.provider = in.provider;
            ty::dataJSON data;
            data.data = in.provider_data.empty() ? std::string("{}") : in.provider_data;
            msg.provider_data = std::make_shared<tl::DataJSON>(tl::DataJSON{data});
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Widget: {
            if (!r.widget) throw std::invalid_argument("widget inline result needs a widget");
            const WidgetSendInfo& w = *r.widget;
            if (w.widget_id != 0) {
                if (!w.html.empty() || w.width != 0 || w.height != 0 || w.public_data || w.allow_queries ||
                    w.shared_widget) {
                    throw std::invalid_argument("a widget registered by id takes only widget_id and chat_data");
                }
                ty::inputBotInlineMessageWidgetId msg;
                msg.widget_id = w.widget_id;
                msg.chat_data = w.chat_data;
                msg.message = r.text;
                if (!r.entities.empty()) msg.entities = entities_list();
                if (r.invert_media) msg.invert_media = true;
                msg.reply_markup = markup_of_result(r);
                return wrap(msg);
            }
            if (w.html.empty()) throw std::invalid_argument("widget needs html or a widget_id");
            if (w.width <= 0 || w.height <= 0) throw std::invalid_argument("widget needs a positive width and height");
            ty::inputBotInlineMessageWidget msg;
            msg.html = w.html;
            msg.width = w.width;
            msg.height = w.height;
            msg.allow_queries = w.allow_queries;
            msg.shared_widget = w.shared_widget;
            msg.public_data = w.public_data;
            msg.chat_data = w.chat_data;
            msg.message = r.text;
            if (!r.entities.empty()) msg.entities = entities_list();
            if (r.invert_media) msg.invert_media = true;
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
        case InlineTextResultInfo::Kind::Game:
            return nullptr;  // games use inputBotInlineMessageGame, built by the caller
        case InlineTextResultInfo::Kind::Photo:
        case InlineTextResultInfo::Kind::Document: {
            ty::inputBotInlineMessageMediaAuto msg;
            msg.message = r.text;
            if (r.invert_media) msg.invert_media = true;
            if (!r.entities.empty()) msg.entities = entities_list();
            msg.reply_markup = markup_of_result(r);
            return wrap(msg);
        }
    }
    throw std::logic_error("unknown inline result kind");
}

std::shared_ptr<tl::InputBotInlineResult> inline_text_result_ptr(const InlineTextResultInfo& r) {
    if (r.id.empty()) throw std::invalid_argument("inline result needs an id");
    switch (r.kind) {
        case InlineTextResultInfo::Kind::Game: {
            if (r.short_name.empty()) throw std::invalid_argument("game inline result needs a short name");
            ty::inputBotInlineResultGame out;
            out.id = r.id;
            out.short_name = r.short_name;
            out.send_message = std::make_shared<tl::InputBotInlineMessage>(
                tl::InputBotInlineMessage{ty::inputBotInlineMessageGame{}});
            return std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{out});
        }
        case InlineTextResultInfo::Kind::Photo: {
            if (r.media.kind != MediaKind::Photo) throw std::invalid_argument("photo inline result needs a photo");
            if (r.type.empty()) throw std::invalid_argument("inline result needs a type");
            ty::inputBotInlineResultPhoto out;
            out.id = r.id;
            out.type = r.type;
            ty::inputPhoto photo;
            photo.id = r.media.id;
            photo.access_hash = r.media.access_hash;
            photo.file_reference = r.media.file_reference;
            out.photo = std::make_shared<tl::InputPhoto>(tl::InputPhoto{photo});
            out.send_message = inline_message_of(r);
            return std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{out});
        }
        case InlineTextResultInfo::Kind::Document: {
            if (r.media.kind != MediaKind::Document) {
                throw std::invalid_argument("document inline result needs a document");
            }
            if (r.type.empty()) throw std::invalid_argument("inline result needs a type");
            ty::inputBotInlineResultDocument out;
            out.id = r.id;
            out.type = r.type;
            if (!r.title.empty()) out.title = r.title;
            if (!r.description.empty()) out.description = r.description;
            ty::inputDocument doc;
            doc.id = r.media.id;
            doc.access_hash = r.media.access_hash;
            doc.file_reference = r.media.file_reference;
            out.document = std::make_shared<tl::InputDocument>(tl::InputDocument{doc});
            out.send_message = inline_message_of(r);
            return std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{out});
        }
        case InlineTextResultInfo::Kind::Text:
        case InlineTextResultInfo::Kind::WebPage:
        case InlineTextResultInfo::Kind::Geo:
        case InlineTextResultInfo::Kind::Venue:
        case InlineTextResultInfo::Kind::Contact:
        case InlineTextResultInfo::Kind::Rich:
        case InlineTextResultInfo::Kind::Invoice:
        case InlineTextResultInfo::Kind::Widget: {
            if (r.type.empty()) throw std::invalid_argument("inline result needs a type");
            ty::inputBotInlineResult out;
            out.id = r.id;
            out.type = r.type;
            if (!r.title.empty()) out.title = r.title;
            if (!r.description.empty()) out.description = r.description;
            out.send_message = inline_message_of(r);
            return std::make_shared<tl::InputBotInlineResult>(tl::InputBotInlineResult{out});
        }
    }
    throw std::logic_error("unknown inline result kind");
}

// Converts the server's InputBotInlineMessageID back to the reference form used for later edits.
InlineMessageRef inline_message_ref_of(const tl::InputBotInlineMessageID& id) {
    InlineMessageRef out;
    if (const auto* v = std::get_if<ty::inputBotInlineMessageID64>(&id.value)) {
        out.use_64 = true;
        out.dc_id = v->dc_id;
        out.owner_id = v->owner_id;
        out.id = v->id;
        out.access_hash = v->access_hash;
    } else if (const auto* v32 = std::get_if<ty::inputBotInlineMessageID>(&id.value)) {
        out.dc_id = v32->dc_id;
        out.id = v32->id;
        out.access_hash = v32->access_hash;
    } else {
        throw std::runtime_error("unexpected InputBotInlineMessageID variant");
    }
    return out;
}

std::shared_ptr<tl::InputBotInlineMessageID> inline_message_id_ptr(const InlineMessageRef& m) {
    if (m.use_64) {
        ty::inputBotInlineMessageID64 v;
        v.dc_id = m.dc_id;
        v.owner_id = m.owner_id;
        v.id = static_cast<std::int32_t>(m.id);
        v.access_hash = m.access_hash;
        return std::make_shared<tl::InputBotInlineMessageID>(tl::InputBotInlineMessageID{v});
    }
    ty::inputBotInlineMessageID v;
    v.dc_id = m.dc_id;
    v.id = m.id;
    v.access_hash = m.access_hash;
    return std::make_shared<tl::InputBotInlineMessageID>(tl::InputBotInlineMessageID{v});
}

WebViewInfo web_view_of(const tl::WebViewResult& result) {
    const auto* u = std::get_if<ty::webViewResultUrl>(&result.value);
    if (u == nullptr) throw std::runtime_error("unexpected WebViewResult variant");
    WebViewInfo out;
    out.url = u->url;
    if (u->query_id) {
        out.has_query_id = true;
        out.query_id = *u->query_id;
    }
    return out;
}

}  // namespace

std::size_t Client::add_chat_user(std::int64_t chat_id, const PeerRef& user, std::int32_t forward_limit) {
    require_session();
    fn::messages_addChatUser req;
    req.chat_id = chat_id;
    req.user_id = input_user_ptr(user);
    req.fwd_limit = forward_limit;
    const tl::messages_InvitedUsers result = call_rpc(req);
    const auto* inv = std::get_if<ty::messages_invitedUsers>(&result.value);
    if (inv == nullptr) throw std::runtime_error("unexpected messages.InvitedUsers variant");
    return inv->missing_invitees.size();
}

bool Client::add_poll_answer(const PeerRef& peer, std::int32_t msg_id, const PollAnswerInfo& answer) {
    require_session();
    fn::messages_addPollAnswer req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    ty::pollAnswer a;
    a.text = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{answer.text, {}}});
    a.option = answer.option;
    req.answer = std::make_shared<tl::PollAnswer>(tl::PollAnswer{a});
    (void)call_rpc(req);
    return true;
}

bool Client::delete_poll_answer(const PeerRef& peer, std::int32_t msg_id, const mtproto::Bytes& option) {
    require_session();
    if (option.empty()) throw std::invalid_argument("delete_poll_answer needs the answer option bytes");
    fn::messages_deletePollAnswer req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    req.option = option;
    (void)call_rpc(req);
    return true;
}

AiTextInfo Client::compose_message_with_ai(const std::string& text, bool proofread, bool emojify,
                                           const std::string& translate_to_lang, const std::string& tone,
                                           const std::vector<TextEntity>& entities) {
    if (tone.empty()) {
        return compose_message_with_ai(text, proofread, emojify, translate_to_lang, AiComposeToneInfo{}, entities);
    }
    AiComposeToneInfo named;
    named.kind = AiComposeToneInfo::Kind::Named;
    named.name = tone;
    return compose_message_with_ai(text, proofread, emojify, translate_to_lang, named, entities);
}

AiTextInfo Client::compose_message_with_ai(const std::string& text, bool proofread, bool emojify,
                                           const std::string& translate_to_lang, const AiComposeToneInfo& tone,
                                           const std::vector<TextEntity>& entities) {
    require_session();
    if (text.empty()) throw std::invalid_argument("compose_message_with_ai needs text");
    fn::messages_composeMessageWithAI req;
    if (proofread) req.proofread = true;
    if (emojify) req.emojify = true;
    std::vector<std::shared_ptr<tl::MessageEntity>> input_entities;
    for (const auto& e : entities) input_entities.push_back(detail::to_tl_entity(e));
    req.text = std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{ty::textWithEntities{text, input_entities}});
    if (!translate_to_lang.empty()) req.translate_to_lang = translate_to_lang;
    switch (tone.kind) {
        case AiComposeToneInfo::Kind::Named: {
            if (tone.name.empty()) break;  // no tone: the server default applies
            ty::inputAiComposeToneDefault t;
            t.tone = tone.name;
            req.tone = std::make_shared<tl::InputAiComposeTone>(tl::InputAiComposeTone{t});
            break;
        }
        case AiComposeToneInfo::Kind::Id: {
            if (tone.id == 0) throw std::invalid_argument("tone id must not be zero");
            ty::inputAiComposeToneID t;
            t.id = tone.id;
            t.access_hash = tone.access_hash;
            req.tone = std::make_shared<tl::InputAiComposeTone>(tl::InputAiComposeTone{t});
            break;
        }
        case AiComposeToneInfo::Kind::Slug: {
            if (tone.slug.empty()) throw std::invalid_argument("tone slug must not be empty");
            ty::inputAiComposeToneSlug t;
            t.slug = tone.slug;
            req.tone = std::make_shared<tl::InputAiComposeTone>(tl::InputAiComposeTone{t});
            break;
        }
        case AiComposeToneInfo::Kind::SingleUse: {
            if (tone.custom_prompt.empty()) throw std::invalid_argument("custom tone prompt must not be empty");
            ty::inputAiComposeToneSingleUse t;
            t.custom_prompt = tone.custom_prompt;
            req.tone = std::make_shared<tl::InputAiComposeTone>(tl::InputAiComposeTone{t});
            break;
        }
    }
    const tl::messages_ComposedMessageWithAI result = call_rpc(req);
    const auto* c = std::get_if<ty::messages_composedMessageWithAI>(&result.value);
    if (c == nullptr) throw std::runtime_error("unexpected messages.ComposedMessageWithAI variant");
    if (!c->result_text) throw std::runtime_error("messages.composedMessageWithAI carried no text");
    const auto* wt = std::get_if<ty::textWithEntities>(&c->result_text->value);
    if (wt == nullptr) throw std::runtime_error("unexpected TextWithEntities variant");
    AiTextInfo out;
    out.text = wt->text;
    for (const auto& e : wt->entities) {
        if (!e) continue;
        if (auto ent = detail::text_entity_from_tl(*e)) out.entities.push_back(*ent);
    }
    if (c->diff_text && *c->diff_text) {
        if (const auto* dt = std::get_if<ty::textWithEntities>(&(*c->diff_text)->value)) {
            out.diff_text = dt->text;
            for (const auto& e : dt->entities) {
                if (!e) continue;
                if (auto ent = detail::text_entity_from_tl(*e)) out.diff_entities.push_back(*ent);
            }
        }
    }
    return out;
}

bool Client::delete_participant_reaction(const PeerRef& peer, std::int32_t msg_id, const PeerRef& participant) {
    require_session();
    fn::messages_deleteParticipantReaction req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    req.participant = input_peer_ptr(*this, participant);
    (void)call_rpc(req);
    return true;
}

bool Client::delete_participant_reactions(const PeerRef& peer, const PeerRef& participant) {
    require_session();
    fn::messages_deleteParticipantReactions req;
    req.peer = input_peer_ptr(*this, peer);
    req.participant = input_peer_ptr(*this, participant);
    return call_rpc(req);
}

AffectedHistoryInfo Client::delete_saved_history(const PeerRef& peer, std::int32_t max_id,
                                                 const std::optional<PeerRef>& parent_peer, std::int32_t min_date,
                                                 std::int32_t max_date) {
    require_session();
    fn::messages_deleteSavedHistory req;
    req.peer = input_peer_ptr(*this, peer);
    req.max_id = max_id;
    if (parent_peer) req.parent_peer = input_peer_ptr(*this, *parent_peer);
    if (min_date) req.min_date = min_date;
    if (max_date) req.max_date = max_date;
    const tl::messages_AffectedHistory result = call_rpc(req);
    const auto* h = std::get_if<ty::messages_affectedHistory>(&result.value);
    if (h == nullptr) throw std::runtime_error("unexpected messages.AffectedHistory variant");
    return AffectedHistoryInfo{h->pts, h->pts_count, h->offset};
}

bool Client::edit_chat_about(const PeerRef& peer, const std::string& about) {
    require_session();
    fn::messages_editChatAbout req;
    req.peer = input_peer_ptr(*this, peer);
    req.about = about;
    return call_rpc(req);
}

bool Client::edit_chat_participant_rank(const PeerRef& peer, const PeerRef& participant, const std::string& rank) {
    require_session();
    fn::messages_editChatParticipantRank req;
    req.peer = input_peer_ptr(*this, peer);
    req.participant = input_peer_ptr(*this, participant);
    req.rank = rank;
    (void)call_rpc(req);
    return true;
}

std::vector<Chat> Client::get_chats(const std::vector<std::int64_t>& chat_ids) {
    require_session();
    fn::messages_getChats req;
    req.id = chat_ids;
    return chat_list(call_rpc(req));
}

DiscussionMessageInfo Client::get_discussion_message(const PeerRef& peer, std::int32_t msg_id) {
    require_session();
    fn::messages_getDiscussionMessage req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    const tl::messages_DiscussionMessage result = call_rpc(req);
    const auto* d = std::get_if<ty::messages_discussionMessage>(&result.value);
    if (d == nullptr) throw std::runtime_error("unexpected messages.DiscussionMessage variant");
    DiscussionMessageInfo out;
    out.unread_count = d->unread_count;
    if (d->max_id) out.max_id = *d->max_id;
    ty::messages_messages wrapped;
    wrapped.messages = d->messages;
    wrapped.chats = d->chats;
    wrapped.users = d->users;
    out.messages = collect_messages(tl::messages_Messages{wrapped});
    return out;
}

InlineBotResultsInfo Client::get_inline_bot_results(const PeerRef& bot, const PeerRef& peer, const std::string& query,
                                                    const std::string& offset) {
    require_session();
    fn::messages_getInlineBotResults req;
    req.bot = input_user_ptr(bot);
    req.peer = input_peer_ptr(*this, peer);
    req.query = query;
    req.offset = offset;
    const tl::messages_BotResults result = call_rpc(req);
    const auto* r = std::get_if<ty::messages_botResults>(&result.value);
    if (r == nullptr) throw std::runtime_error("unexpected messages.BotResults variant");
    cache_users_chats(r->users, {});
    InlineBotResultsInfo out;
    out.query_id = r->query_id;
    if (r->next_offset) out.next_offset = *r->next_offset;
    out.cache_time = r->cache_time;
    out.gallery = r->gallery;
    if (r->switch_pm && *r->switch_pm) {
        if (const auto* pm = std::get_if<ty::inlineBotSwitchPM>(&(*r->switch_pm)->value)) {
            out.switch_pm = InlineBotSwitchPMInfo{pm->text, pm->start_param, {}};
        }
    }
    if (r->switch_webview && *r->switch_webview) {
        if (const auto* wv = std::get_if<ty::inlineBotWebView>(&(*r->switch_webview)->value)) {
            out.switch_webview = InlineBotSwitchPMInfo{wv->text, {}, wv->url};
        }
    }
    for (const auto& br : r->results) {
        if (!br) continue;
        out.results.push_back(inline_result_of(*br));
    }
    return out;
}

std::vector<MessageViewInfo> Client::get_messages_views(const PeerRef& peer, const std::vector<std::int32_t>& msg_ids,
                                                        bool increment) {
    require_session();
    fn::messages_getMessagesViews req;
    req.peer = input_peer_ptr(*this, peer);
    req.id = msg_ids;
    req.increment = increment;
    const tl::messages_MessageViews result = call_rpc(req);
    const auto* v = std::get_if<ty::messages_messageViews>(&result.value);
    if (v == nullptr) throw std::runtime_error("unexpected messages.MessageViews variant");
    cache_users_chats(v->users, v->chats);
    std::vector<MessageViewInfo> out;
    for (const auto& item : v->views) {
        if (!item) continue;
        const auto* mv = std::get_if<ty::messageViews>(&item->value);
        if (mv == nullptr) continue;
        MessageViewInfo info;
        if (mv->views) info.views = *mv->views;
        if (mv->forwards) info.forwards = *mv->forwards;
        if (mv->replies && *mv->replies) {
            if (const auto* r = std::get_if<ty::messageReplies>(&(*mv->replies)->value)) info.replies = r->replies;
        }
        out.push_back(info);
    }
    return out;
}

namespace {

// Sticker document with its emoji. Empty documents give nullopt.
std::optional<StickerDocInfo> sticker_doc_of(const std::shared_ptr<tl::Document>& doc) {
    if (!doc) return std::nullopt;
    auto media = detail::media_info_from_document(*doc);
    if (!media) return std::nullopt;
    StickerDocInfo out;
    out.media = *media;
    if (const auto* d = std::get_if<ty::document>(&doc->value)) {
        for (const auto& attr : d->attributes) {
            if (!attr) continue;
            if (const auto* sa = std::get_if<ty::documentAttributeSticker>(&attr->value)) out.emoji = sa->alt;
        }
    }
    return out;
}

std::vector<StickerPackInfo> packs_of(const std::vector<std::shared_ptr<tl::StickerPack>>& packs) {
    std::vector<StickerPackInfo> out;
    for (const auto& p : packs) {
        if (!p) continue;
        if (const auto* pack = std::get_if<ty::stickerPack>(&p->value)) {
            out.push_back(StickerPackInfo{pack->emoticon, pack->documents});
        }
    }
    return out;
}

std::vector<StickerKeywordInfo> keywords_of(const std::vector<std::shared_ptr<tl::StickerKeyword>>& keywords) {
    std::vector<StickerKeywordInfo> out;
    for (const auto& k : keywords) {
        if (!k) continue;
        if (const auto* kw = std::get_if<ty::stickerKeyword>(&k->value)) {
            out.push_back(StickerKeywordInfo{kw->document_id, kw->keyword});
        }
    }
    return out;
}

std::vector<MediaInfo> documents_of(const std::vector<std::shared_ptr<tl::Document>>& docs) {
    std::vector<MediaInfo> out;
    for (const auto& d : docs) {
        if (!d) continue;
        if (auto m = detail::media_info_from_document(*d)) out.push_back(*m);
    }
    return out;
}

StickerSetInfo set_of(const std::shared_ptr<tl::StickerSet>& set) {
    if (!set) throw std::runtime_error("sticker set entry has no set");
    const auto* meta = std::get_if<ty::stickerSet>(&set->value);
    if (meta == nullptr) throw std::runtime_error("unexpected sticker set variant");
    return detail::sticker_set_info_from_tl(*meta);
}

// Decodes every messages.StickerSetCovered variant. The covers, documents, packs and keywords are filled only for
// the variants that carry them.
StickerSetCoveredInfo covered_of(const tl::StickerSetCovered& c) {
    StickerSetCoveredInfo out;
    if (const auto* one = std::get_if<ty::stickerSetCovered>(&c.value)) {
        out.set = set_of(one->set);
        if (one->cover) out.covers = documents_of({one->cover});
    } else if (const auto* multi = std::get_if<ty::stickerSetMultiCovered>(&c.value)) {
        out.set = set_of(multi->set);
        out.covers = documents_of(multi->covers);
    } else if (const auto* full = std::get_if<ty::stickerSetFullCovered>(&c.value)) {
        out.set = set_of(full->set);
        out.packs = packs_of(full->packs);
        out.keywords = keywords_of(full->keywords);
        out.documents = documents_of(full->documents);
    } else if (const auto* none = std::get_if<ty::stickerSetNoCovered>(&c.value)) {
        out.set = set_of(none->set);
    } else {
        throw std::runtime_error("unexpected messages.StickerSetCovered variant");
    }
    return out;
}

}  // namespace

StickerSetsPageInfo Client::get_my_stickers(std::int64_t offset_id, std::int32_t limit) {
    require_session();
    if (limit <= 0) throw std::invalid_argument("get_my_stickers limit must be positive");
    fn::messages_getMyStickers req;
    req.offset_id = offset_id;
    req.limit = limit;
    const tl::messages_MyStickers result = call_rpc(req);
    const auto* m = std::get_if<ty::messages_myStickers>(&result.value);
    if (m == nullptr) throw std::runtime_error("unexpected messages.MyStickers variant");
    StickerSetsPageInfo out;
    out.count = m->count;
    for (const auto& s : m->sets) {
        if (s) out.sets.push_back(covered_of(*s));
    }
    return out;
}

PeerDialogsInfo Client::get_peer_dialogs(const std::vector<DialogPeerInput>& peers) {
    require_session();
    if (peers.empty()) throw std::invalid_argument("get_peer_dialogs needs at least one peer");
    fn::messages_getPeerDialogs req;
    for (const auto& p : peers) {
        switch (p.kind) {
            case DialogPeerInput::Kind::Peer: {
                ty::inputDialogPeer d;
                d.peer = input_peer_ptr(*this, p.peer);
                req.peers.push_back(std::make_shared<tl::InputDialogPeer>(tl::InputDialogPeer{d}));
                break;
            }
            case DialogPeerInput::Kind::Folder: {
                if (p.folder_id < 1) throw std::invalid_argument("folder id must be positive");
                ty::inputDialogPeerFolder d;
                d.folder_id = p.folder_id;
                req.peers.push_back(std::make_shared<tl::InputDialogPeer>(tl::InputDialogPeer{d}));
                break;
            }
            case DialogPeerInput::Kind::Community: {
                if (p.peer.kind != PeerKind::Channel) throw std::invalid_argument("community must be a channel");
                ty::inputDialogPeerCommunity d;
                d.community = std::make_shared<tl::InputChannel>(
                    tl::InputChannel{ty::inputChannel{p.peer.id, p.peer.access_hash}});
                req.peers.push_back(std::make_shared<tl::InputDialogPeer>(tl::InputDialogPeer{d}));
                break;
            }
        }
    }
    const tl::messages_PeerDialogs result = call_rpc(req);
    const auto* pd = std::get_if<ty::messages_peerDialogs>(&result.value);
    if (pd == nullptr) throw std::runtime_error("unexpected messages.PeerDialogs variant");
    ty::messages_messages wrapped;
    wrapped.messages = pd->messages;
    wrapped.chats = pd->chats;
    wrapped.users = pd->users;
    PeerDialogsInfo out;
    out.messages = collect_messages(tl::messages_Messages{wrapped});
    out.dialogs = detail::dialogs_from_tl(pd->dialogs, out.messages, cache_);
    if (!pd->state) throw std::runtime_error("messages.PeerDialogs without state");
    const auto* st = std::get_if<ty::updates_state>(&pd->state->value);
    if (st == nullptr) throw std::runtime_error("unexpected updates.State variant");
    out.pts = st->pts;
    out.qts = st->qts;
    out.date = st->date;
    out.seq = st->seq;
    out.unread_count = st->unread_count;
    return out;
}

PeerSettingsInfo Client::get_peer_settings(const PeerRef& peer) {
    require_session();
    fn::messages_getPeerSettings req;
    req.peer = input_peer_ptr(*this, peer);
    const tl::messages_PeerSettings result = call_rpc(req);
    const auto* ps = std::get_if<ty::messages_peerSettings>(&result.value);
    if (ps == nullptr) throw std::runtime_error("unexpected messages.PeerSettings variant");
    cache_users_chats(ps->users, ps->chats);
    PeerSettingsInfo out;
    if (ps->settings) {
        const auto* s = std::get_if<ty::peerSettings>(&ps->settings->value);
        if (s == nullptr) throw std::runtime_error("unexpected PeerSettings variant");
        out.report_spam = s->report_spam;
        out.add_contact = s->add_contact;
        out.block_contact = s->block_contact;
        out.share_contact = s->share_contact;
        out.need_contacts_exception = s->need_contacts_exception;
        out.report_geo = s->report_geo;
        out.autoarchived = s->autoarchived;
        out.invite_members = s->invite_members;
        out.request_chat_broadcast = s->request_chat_broadcast;
        out.business_bot_paused = s->business_bot_paused;
        out.business_bot_can_reply = s->business_bot_can_reply;
        out.geo_distance = s->geo_distance;
        out.request_chat_title = s->request_chat_title;
        out.request_chat_date = s->request_chat_date;
        out.business_bot_id = s->business_bot_id;
        out.business_bot_manage_url = s->business_bot_manage_url;
        out.charge_paid_message_stars = s->charge_paid_message_stars;
        out.registration_month = s->registration_month;
        out.phone_country = s->phone_country;
        out.name_change_date = s->name_change_date;
        out.photo_change_date = s->photo_change_date;
    }
    return out;
}

std::vector<Message> Client::get_personal_channel_history(const PeerRef& user, std::int32_t limit, std::int32_t max_id,
                                                          std::int32_t min_id, std::int64_t hash) {
    require_session();
    fn::messages_getPersonalChannelHistory req;
    req.user_id = input_user_ptr(user);
    req.limit = limit;
    req.max_id = max_id;
    req.min_id = min_id;
    req.hash = hash;
    return collect_messages(call_rpc(req));
}

std::vector<DialogInfo> Client::get_pinned_dialogs(std::int32_t folder_id) {
    require_session();
    fn::messages_getPinnedDialogs req;
    req.folder_id = folder_id;
    const tl::messages_PeerDialogs result = call_rpc(req);
    const auto* pd = std::get_if<ty::messages_peerDialogs>(&result.value);
    if (pd == nullptr) throw std::runtime_error("unexpected messages.PeerDialogs variant");
    cache_users_chats(pd->users, pd->chats);
    ty::messages_messages wrapped;
    wrapped.messages = pd->messages;
    wrapped.chats = pd->chats;
    wrapped.users = pd->users;
    const std::vector<Message> messages = collect_messages(tl::messages_Messages{wrapped});
    return detail::dialogs_from_tl(pd->dialogs, messages, cache_);
}

StickerListInfo Client::get_recent_stickers(bool attached, std::int64_t hash) {
    require_session();
    fn::messages_getRecentStickers req;
    if (attached) req.attached = true;
    req.hash = hash;
    const tl::messages_RecentStickers result = call_rpc(req);
    StickerListInfo out;
    if (std::holds_alternative<ty::messages_recentStickersNotModified>(result.value)) {
        out.not_modified = true;
        return out;
    }
    const auto* r = std::get_if<ty::messages_recentStickers>(&result.value);
    if (r == nullptr) throw std::runtime_error("unexpected messages.RecentStickers variant");
    out.hash = r->hash;
    out.packs = packs_of(r->packs);
    for (std::size_t i = 0; i < r->stickers.size(); ++i) {
        auto sd = sticker_doc_of(r->stickers[i]);
        if (!sd) continue;
        out.stickers.push_back(std::move(*sd));
        // dates is parallel to stickers; keep the pair aligned when a document is skipped.
        if (i < r->dates.size()) out.dates.push_back(r->dates[i]);
    }
    return out;
}

SavedDialogsPage Client::get_saved_dialogs(bool exclude_pinned, const std::optional<PeerRef>& parent_peer,
                                           std::int32_t offset_date, std::int32_t offset_id, const PeerRef& offset_peer,
                                           std::int32_t limit, std::int64_t hash) {
    require_session();
    fn::messages_getSavedDialogs req;
    if (exclude_pinned) req.exclude_pinned = true;
    if (parent_peer) req.parent_peer = input_peer_ptr(*this, *parent_peer);
    req.offset_date = offset_date;
    req.offset_id = offset_id;
    req.offset_peer = input_peer_ptr(*this, offset_peer);
    req.limit = limit;
    req.hash = hash;
    const tl::messages_SavedDialogs result = call_rpc(req);
    SavedDialogsPage out;
    if (const auto* d = std::get_if<ty::messages_savedDialogs>(&result.value)) {
        cache_users_chats(d->users, d->chats);
        out.count = static_cast<std::int32_t>(d->dialogs.size());
        ty::messages_messages wrapped;
        wrapped.messages = d->messages;
        wrapped.chats = d->chats;
        wrapped.users = d->users;
        out.messages = collect_messages(tl::messages_Messages{wrapped});
    } else if (const auto* s = std::get_if<ty::messages_savedDialogsSlice>(&result.value)) {
        cache_users_chats(s->users, s->chats);
        out.count = s->count;
        ty::messages_messages wrapped;
        wrapped.messages = s->messages;
        wrapped.chats = s->chats;
        wrapped.users = s->users;
        out.messages = collect_messages(tl::messages_Messages{wrapped});
    } else {
        throw std::runtime_error("unexpected messages.SavedDialogs variant");
    }
    return out;
}

SavedDialogsPage Client::get_saved_dialogs_by_id(const std::vector<PeerRef>& peers,
                                                 const std::optional<PeerRef>& parent_peer) {
    require_session();
    fn::messages_getSavedDialogsByID req;
    if (parent_peer) req.parent_peer = input_peer_ptr(*this, *parent_peer);
    for (const auto& p : peers) req.ids.push_back(input_peer_ptr(*this, p));
    const tl::messages_SavedDialogs result = call_rpc(req);
    SavedDialogsPage out;
    if (const auto* d = std::get_if<ty::messages_savedDialogs>(&result.value)) {
        out.count = static_cast<std::int32_t>(d->dialogs.size());
        ty::messages_messages wrapped;
        wrapped.messages = d->messages;
        wrapped.chats = d->chats;
        wrapped.users = d->users;
        out.messages = collect_messages(tl::messages_Messages{wrapped});
    } else if (const auto* s = std::get_if<ty::messages_savedDialogsSlice>(&result.value)) {
        out.count = s->count;
        ty::messages_messages wrapped;
        wrapped.messages = s->messages;
        wrapped.chats = s->chats;
        wrapped.users = s->users;
        out.messages = collect_messages(tl::messages_Messages{wrapped});
    } else {
        throw std::runtime_error("unexpected messages.SavedDialogs variant");
    }
    return out;
}

std::vector<Message> Client::get_saved_history(const PeerRef& peer, std::int32_t offset_id, std::int32_t offset_date,
                                               std::int32_t add_offset, std::int32_t limit, std::int32_t max_id,
                                               std::int32_t min_id, std::int64_t hash,
                                               const std::optional<PeerRef>& parent_peer) {
    require_session();
    fn::messages_getSavedHistory req;
    req.peer = input_peer_ptr(*this, peer);
    if (parent_peer) req.parent_peer = input_peer_ptr(*this, *parent_peer);
    req.offset_id = offset_id;
    req.offset_date = offset_date;
    req.add_offset = add_offset;
    req.limit = limit;
    req.max_id = max_id;
    req.min_id = min_id;
    req.hash = hash;
    return collect_messages(call_rpc(req));
}

std::vector<SearchCounterInfo> Client::get_search_counters(
    const PeerRef& peer, const std::vector<std::shared_ptr<tl::MessagesFilter>>& filters,
    const std::optional<std::int32_t>& top_msg_id) {
    require_session();
    if (filters.empty()) throw std::invalid_argument("get_search_counters needs at least one filter");
    fn::messages_getSearchCounters req;
    req.peer = input_peer_ptr(*this, peer);
    req.filters = filters;
    if (top_msg_id) req.top_msg_id = *top_msg_id;
    const std::vector<std::shared_ptr<tl::messages_SearchCounter>> result = call_rpc(req);
    std::vector<SearchCounterInfo> out;
    for (const auto& c : result) {
        if (!c) continue;
        if (const auto* sc = std::get_if<ty::messages_searchCounter>(&c->value)) {
            out.push_back(SearchCounterInfo{sc->count, sc->inexact});
        }
    }
    return out;
}

std::vector<MessageRangeInfo> Client::get_split_ranges() {
    require_session();
    fn::messages_getSplitRanges req;
    const std::vector<std::shared_ptr<tl::MessageRange>> result = call_rpc(req);
    std::vector<MessageRangeInfo> out;
    for (const auto& r : result) {
        if (!r) continue;
        if (const auto* mr = std::get_if<ty::messageRange>(&r->value)) out.push_back(MessageRangeInfo{mr->min_id, mr->max_id});
    }
    return out;
}

StickerInstallInfo Client::install_sticker_set(const StickerSetRef& set, bool archived) {
    require_session();
    fn::messages_installStickerSet req;
    req.stickerset = sticker_set_ptr(set);
    req.archived = archived;
    const tl::messages_StickerSetInstallResult result = call_rpc(req);
    StickerInstallInfo out;
    if (const auto* a = std::get_if<ty::messages_stickerSetInstallResultArchive>(&result.value)) {
        out.archived = true;
        out.archived_sets = a->sets.size();
    }
    return out;
}

bool Client::uninstall_sticker_set(const StickerSetRef& set) {
    require_session();
    fn::messages_uninstallStickerSet req;
    req.stickerset = sticker_set_ptr(set);
    return call_rpc(req);
}

bool Client::reorder_sticker_sets(const std::vector<std::int64_t>& order, bool masks, bool emojis) {
    require_session();
    fn::messages_reorderStickerSets req;
    if (masks) req.masks = true;
    if (emojis) req.emojis = true;
    req.order = order;
    return call_rpc(req);
}

StickerSetsPageInfo Client::search_sticker_sets(const std::string& query, bool exclude_featured, std::int64_t hash) {
    require_session();
    fn::messages_searchStickerSets req;
    if (exclude_featured) req.exclude_featured = true;
    req.q = query;
    req.hash = hash;
    const tl::messages_FoundStickerSets result = call_rpc(req);
    StickerSetsPageInfo out;
    if (std::holds_alternative<ty::messages_foundStickerSetsNotModified>(result.value)) {
        out.not_modified = true;
        return out;
    }
    const auto* f = std::get_if<ty::messages_foundStickerSets>(&result.value);
    if (f == nullptr) throw std::runtime_error("unexpected messages.FoundStickerSets variant");
    out.hash = f->hash;
    for (const auto& s : f->sets) {
        if (s) out.sets.push_back(covered_of(*s));
    }
    return out;
}

StickerListInfo Client::search_stickers(const std::string& query, const std::string& emoticon,
                                        const std::vector<std::string>& lang_codes, bool emojis, std::int32_t offset,
                                        std::int32_t limit, std::int64_t hash) {
    require_session();
    fn::messages_searchStickers req;
    if (emojis) req.emojis = true;
    req.q = query;
    req.emoticon = emoticon;
    req.lang_code = lang_codes;
    req.offset = offset;
    req.limit = limit;
    req.hash = hash;
    const tl::messages_FoundStickers result = call_rpc(req);
    StickerListInfo out;
    if (const auto* nm = std::get_if<ty::messages_foundStickersNotModified>(&result.value)) {
        out.not_modified = true;
        if (nm->next_offset) out.next_offset = *nm->next_offset;
        return out;
    }
    const auto* f = std::get_if<ty::messages_foundStickers>(&result.value);
    if (f == nullptr) throw std::runtime_error("unexpected messages.FoundStickers variant");
    out.hash = f->hash;
    for (const auto& d : f->stickers) {
        if (auto sd = sticker_doc_of(d)) out.stickers.push_back(std::move(*sd));
    }
    if (f->next_offset) out.next_offset = *f->next_offset;
    return out;
}

AffectedHistoryInfo Client::read_mentions(const PeerRef& peer, const std::optional<std::int32_t>& top_msg_id) {
    require_session();
    fn::messages_readMentions req;
    req.peer = input_peer_ptr(*this, peer);
    if (top_msg_id) req.top_msg_id = *top_msg_id;
    const tl::messages_AffectedHistory result = call_rpc(req);
    const auto* h = std::get_if<ty::messages_affectedHistory>(&result.value);
    if (h == nullptr) throw std::runtime_error("unexpected messages.AffectedHistory variant");
    return AffectedHistoryInfo{h->pts, h->pts_count, h->offset};
}

AffectedHistoryInfo Client::read_reactions(const PeerRef& peer, const std::optional<std::int32_t>& top_msg_id,
                                           const std::optional<PeerRef>& saved_peer) {
    require_session();
    fn::messages_readReactions req;
    req.peer = input_peer_ptr(*this, peer);
    if (top_msg_id) req.top_msg_id = *top_msg_id;
    if (saved_peer) req.saved_peer_id = input_peer_ptr(*this, *saved_peer);
    const tl::messages_AffectedHistory result = call_rpc(req);
    const auto* h = std::get_if<ty::messages_affectedHistory>(&result.value);
    if (h == nullptr) throw std::runtime_error("unexpected messages.AffectedHistory variant");
    return AffectedHistoryInfo{h->pts, h->pts_count, h->offset};
}

WebViewInfo Client::request_app_web_view(const PeerRef& peer, const BotAppRef& app, const std::string& start_param,
                                         const std::string& platform, bool write_allowed) {
    require_session();
    fn::messages_requestAppWebView req;
    req.peer = input_peer_ptr(*this, peer);
    if (app.by_short_name) {
        ty::inputBotAppShortName s;
        s.bot_id = input_user_ptr(app.bot);
        s.short_name = app.short_name;
        req.app = std::make_shared<tl::InputBotApp>(tl::InputBotApp{s});
    } else {
        ty::inputBotAppID a;
        a.id = app.id;
        a.access_hash = app.access_hash;
        req.app = std::make_shared<tl::InputBotApp>(tl::InputBotApp{a});
    }
    if (!start_param.empty()) req.start_param = start_param;
    req.platform = platform;
    if (write_allowed) req.write_allowed = true;
    return web_view_of(call_rpc(req));
}

WebViewInfo Client::request_main_web_view(const PeerRef& peer, const PeerRef& bot, const std::string& start_param,
                                          const std::string& platform) {
    require_session();
    fn::messages_requestMainWebView req;
    req.peer = input_peer_ptr(*this, peer);
    req.bot = input_user_ptr(bot);
    if (!start_param.empty()) req.start_param = start_param;
    req.platform = platform;
    return web_view_of(call_rpc(req));
}

WebViewInfo Client::request_simple_web_view(const PeerRef& bot, const std::string& url, const std::string& start_param,
                                            const std::string& platform) {
    require_session();
    fn::messages_requestSimpleWebView req;
    req.bot = input_user_ptr(bot);
    if (!url.empty()) req.url = url;
    if (!start_param.empty()) req.start_param = start_param;
    req.platform = platform;
    return web_view_of(call_rpc(req));
}

WebViewInfo Client::request_web_view(const PeerRef& peer, const PeerRef& bot, const std::string& url,
                                     const std::string& start_param, const std::string& platform) {
    require_session();
    fn::messages_requestWebView req;
    req.peer = input_peer_ptr(*this, peer);
    req.bot = input_user_ptr(bot);
    if (!url.empty()) req.url = url;
    if (!start_param.empty()) req.start_param = start_param;
    req.platform = platform;
    return web_view_of(call_rpc(req));
}

bool Client::save_default_send_as(const PeerRef& peer, const PeerRef& send_as) {
    require_session();
    fn::messages_saveDefaultSendAs req;
    req.peer = input_peer_ptr(*this, peer);
    req.send_as = send_as_ptr(*this, send_as);
    return call_rpc(req);
}

bool Client::save_gif(const DocumentRef& gif, bool unsave) {
    require_session();
    fn::messages_saveGif req;
    req.id = input_document_ptr(gif);
    req.unsave = unsave;
    return call_rpc(req);
}

bool Client::send_inline_bot_result(const PeerRef& peer, std::int64_t query_id, const std::string& result_id,
                                    const InlineBotSendOptions& options) {
    require_session();
    if (result_id.empty()) throw std::invalid_argument("send_inline_bot_result needs a result id");
    if (options.schedule_date < 0) throw std::invalid_argument("schedule_date must not be negative");
    fn::messages_sendInlineBotResult req;
    req.peer = input_peer_ptr(*this, peer);
    req.random_id = detail::secure_random_i64();
    req.query_id = query_id;
    req.id = result_id;
    if (options.silent) req.silent = true;
    if (options.background) req.background = true;
    if (options.clear_draft) req.clear_draft = true;
    if (options.hide_via) req.hide_via = true;
    if (options.schedule_date != 0) req.schedule_date = options.schedule_date;
    if (options.reply_to_msg_id) {
        req.reply_to = std::make_shared<tl::InputReplyTo>(
            tl::InputReplyTo{ty::inputReplyToMessage{*options.reply_to_msg_id}});
    }
    if (options.send_as) req.send_as = input_peer_ptr(*this, *options.send_as);
    if (options.quick_reply_shortcut_id) {
        req.quick_reply_shortcut = std::make_shared<tl::InputQuickReplyShortcut>(
            tl::InputQuickReplyShortcut{ty::inputQuickReplyShortcutId{*options.quick_reply_shortcut_id}});
    } else if (options.quick_reply_shortcut) {
        if (options.quick_reply_shortcut->empty()) throw std::invalid_argument("quick reply shortcut name is empty");
        req.quick_reply_shortcut = std::make_shared<tl::InputQuickReplyShortcut>(
            tl::InputQuickReplyShortcut{ty::inputQuickReplyShortcut{*options.quick_reply_shortcut}});
    }
    if (options.allow_paid_stars) {
        if (*options.allow_paid_stars <= 0) throw std::invalid_argument("allow_paid_stars must be positive");
        req.allow_paid_stars = *options.allow_paid_stars;
    }
    (void)call_rpc(req);
    return true;
}

bool Client::send_paid_reaction(const PeerRef& peer, std::int32_t msg_id, std::int32_t count,
                                const PaidReactionPrivacyInfo& privacy) {
    require_session();
    if (count <= 0) throw std::invalid_argument("send_paid_reaction count must be positive");
    fn::messages_sendPaidReaction req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    req.count = count;
    req.random_id = detail::secure_random_i64();
    switch (privacy.kind) {
        case PaidReactionPrivacyInfo::Kind::Default:
            break;
        case PaidReactionPrivacyInfo::Kind::Anonymous:
            req.private_ = std::make_shared<tl::PaidReactionPrivacy>(tl::PaidReactionPrivacy{ty::paidReactionPrivacyAnonymous{}});
            break;
        case PaidReactionPrivacyInfo::Kind::Peer:
            req.private_ = std::make_shared<tl::PaidReactionPrivacy>(
                tl::PaidReactionPrivacy{ty::paidReactionPrivacyPeer{input_peer_ptr(*this, privacy.peer)}});
            break;
    }
    (void)call_rpc(req);
    return true;
}

bool Client::send_screenshot_notification(const PeerRef& peer, std::int32_t reply_to_msg_id) {
    require_session();
    fn::messages_sendScreenshotNotification req;
    req.peer = input_peer_ptr(*this, peer);
    ty::inputReplyToMessage reply;
    reply.reply_to_msg_id = reply_to_msg_id;
    req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{reply});
    req.random_id = detail::secure_random_i64();
    (void)call_rpc(req);
    return true;
}

std::optional<InlineMessageRef> Client::send_web_view_result_message(const std::string& bot_query_id,
                                                                    const InlineTextResultInfo& result) {
    require_session();
    if (bot_query_id.empty()) throw std::invalid_argument("send_web_view_result_message needs the bot query id");
    fn::messages_sendWebViewResultMessage req;
    req.bot_query_id = bot_query_id;
    InlineTextResultInfo resolved;
    const InlineTextResultInfo* to_send = &result;
    if (result.kind == InlineTextResultInfo::Kind::Rich && result.rich_message) {
        resolved = result;
        resolved.rich_message = with_uploaded_rich_files(*this, *result.rich_message,
                                                         [this](fn::messages_uploadMedia& up) { return call_rpc(up); });
        to_send = &resolved;
    }
    req.result = inline_text_result_ptr(*to_send);
    const tl::WebViewMessageSent sent = call_rpc(req);
    const auto* s = std::get_if<ty::webViewMessageSent>(&sent.value);
    if (s == nullptr) throw std::runtime_error("unexpected WebViewMessageSent variant");
    if (!s->msg_id || !*s->msg_id) return std::nullopt;
    return inline_message_ref_of(**s->msg_id);
}

InlineMessageRef Client::set_bot_guest_chat_result(std::int64_t query_id, const InlineTextResultInfo& result) {
    require_session();
    fn::messages_setBotGuestChatResult req;
    req.query_id = query_id;
    InlineTextResultInfo resolved;
    const InlineTextResultInfo* to_send = &result;
    if (result.kind == InlineTextResultInfo::Kind::Rich && result.rich_message) {
        resolved = result;
        resolved.rich_message = with_uploaded_rich_files(*this, *result.rich_message,
                                                         [this](fn::messages_uploadMedia& up) { return call_rpc(up); });
        to_send = &resolved;
    }
    req.result = inline_text_result_ptr(*to_send);
    const tl::InputBotInlineMessageID id = call_rpc(req);
    return inline_message_ref_of(id);
}

bool Client::set_bot_shipping_results(std::int64_t query_id, const std::optional<std::string>& error,
                                      const std::vector<ShippingOptionInfo>& options) {
    require_session();
    fn::messages_setBotShippingResults req;
    req.query_id = query_id;
    if (error) req.error_ = *error;
    if (!options.empty()) {
        std::vector<std::shared_ptr<tl::ShippingOption>> list;
        for (const auto& o : options) {
            ty::shippingOption so;
            so.id = o.id;
            so.title = o.title;
            for (const auto& p : o.prices) {
                so.prices.push_back(std::make_shared<tl::LabeledPrice>(tl::LabeledPrice{ty::labeledPrice{p.label, p.amount}}));
            }
            list.push_back(std::make_shared<tl::ShippingOption>(tl::ShippingOption{so}));
        }
        req.shipping_options = std::move(list);
    }
    return call_rpc(req);
}

bool Client::set_inline_game_score(const InlineMessageRef& message, const PeerRef& user, std::int32_t score,
                                   bool edit_message, bool force) {
    require_session();
    fn::messages_setInlineGameScore req;
    if (edit_message) req.edit_message = true;
    if (force) req.force = true;
    req.id = inline_message_id_ptr(message);
    req.user_id = input_user_ptr(user);
    req.score = score;
    return call_rpc(req);
}

bool Client::start_bot(const PeerRef& bot, const PeerRef& peer, const std::string& start_param) {
    require_session();
    fn::messages_startBot req;
    req.bot = input_user_ptr(bot);
    req.peer = input_peer_ptr(*this, peer);
    req.random_id = detail::secure_random_i64();
    req.start_param = start_param;
    (void)call_rpc(req);
    return true;
}

AiTextInfo Client::summarize_text(const PeerRef& peer, std::int32_t msg_id, const std::optional<std::string>& to_lang,
                                  const std::optional<std::string>& tone) {
    require_session();
    fn::messages_summarizeText req;
    req.peer = input_peer_ptr(*this, peer);
    req.id = msg_id;
    if (to_lang) req.to_lang = *to_lang;
    if (tone) req.tone = *tone;
    const tl::TextWithEntities result = call_rpc(req);
    const auto* t = std::get_if<ty::textWithEntities>(&result.value);
    if (t == nullptr) throw std::runtime_error("unexpected TextWithEntities variant");
    AiTextInfo out;
    out.text = t->text;
    for (const auto& e : t->entities) {
        if (!e) continue;
        if (auto ent = detail::text_entity_from_tl(*e)) out.entities.push_back(*ent);
    }
    return out;
}

bool Client::toggle_no_forwards(const PeerRef& peer, bool enabled, const std::optional<std::int32_t>& request_msg_id) {
    require_session();
    fn::messages_toggleNoForwards req;
    req.peer = input_peer_ptr(*this, peer);
    req.enabled = enabled;
    if (request_msg_id) req.request_msg_id = *request_msg_id;
    (void)call_rpc(req);
    return true;
}

bool Client::toggle_suggested_post_approval(const PeerRef& peer, std::int32_t msg_id, bool reject,
                                            std::int32_t schedule_date, const std::optional<std::string>& reject_comment) {
    require_session();
    fn::messages_toggleSuggestedPostApproval req;
    req.peer = input_peer_ptr(*this, peer);
    req.msg_id = msg_id;
    if (reject) req.reject = true;
    if (schedule_date != 0) req.schedule_date = schedule_date;
    if (reject_comment) req.reject_comment = *reject_comment;
    (void)call_rpc(req);
    return true;
}

// ---- ephemeral, stickers, birthday, top peers

bool Client::ephemeral_delete_message(const PeerRef& receiver, std::int32_t msg_id, const std::optional<PeerRef>& peer) {
    require_session();
    fn::ephemeral_deleteMessage req;
    req.receiver_id = input_user_ptr(receiver);
    req.id = msg_id;
    if (peer) req.peer = input_peer_ptr(*this, *peer);
    return call_rpc(req);
}

namespace {

// Rejects option combinations before any request is sent. text may be empty only when a media or rich message is set.
void check_ephemeral_options(const std::string& text, const EphemeralMessageOptions& o, bool edit) {
    const int media_forms = (o.media ? 1 : 0) + (o.media_path ? 1 : 0) + (o.uploaded_media ? 1 : 0);
    if (media_forms > 1) {
        throw std::invalid_argument("ephemeral message takes one of media, media_path or uploaded_media");
    }
    if (o.media_path && o.media_path->empty()) throw std::invalid_argument("media_path is empty");
    if (text.empty() && media_forms == 0 && !o.rich_message) {
        throw std::invalid_argument("ephemeral message needs text, a media or a rich message");
    }
    if (o.rich_message && !text.empty()) {
        throw std::invalid_argument("a rich message replaces the text; leave text empty");
    }
    if (o.inline_markup && o.reply_markup) {
        throw std::invalid_argument("ephemeral message takes one of inline_markup or reply_markup, not both");
    }
    if (o.media && o.media->kind != MediaKind::Photo && o.media->kind != MediaKind::Document) {
        throw std::invalid_argument("ephemeral messages can only send cached photos or documents");
    }
    if (edit && o.noforwards) throw std::invalid_argument("noforwards applies to ephemeral_send_message only");
    if (edit && o.anchor) throw std::invalid_argument("anchor applies to ephemeral_send_message only");
    if (edit && o.reply_to_msg_id) throw std::invalid_argument("reply_to_msg_id applies to ephemeral_send_message only");
}

// Input media of an ephemeral message. media_path is uploaded through the shared save_file path first, so progress
// is reported through upload_progress. Returns nullptr when the message has no media.
std::shared_ptr<tl::InputMedia> ephemeral_media_input(Client& client, const EphemeralMessageOptions& o) {
    if (o.media) return cached_input_media(*o.media);
    if (o.uploaded_media) return detail::uploaded_input_media(*o.uploaded_media, o.media_as_photo);
    if (o.media_path) {
        UploadedFile uploaded = o.upload_progress ? client.save_file(*o.media_path, o.upload_progress)
                                                  : client.save_file(*o.media_path);
        return detail::uploaded_input_media(uploaded, o.media_as_photo);
    }
    return nullptr;
}

std::optional<std::shared_ptr<tl::ReplyMarkup>> ephemeral_markup_of(const EphemeralMessageOptions& o) {
    if (o.inline_markup) return detail::to_tl_inline_markup(*o.inline_markup);
    if (o.reply_markup) return detail::to_tl_reply_markup(*o.reply_markup);
    return std::nullopt;
}

std::optional<std::vector<std::shared_ptr<tl::MessageEntity>>> ephemeral_entities_of(const EphemeralMessageOptions& o) {
    if (o.entities.empty()) return std::nullopt;
    std::vector<std::shared_ptr<tl::MessageEntity>> list;
    for (const auto& e : o.entities) list.push_back(detail::to_tl_entity(e));
    return list;
}

}  // namespace

bool Client::ephemeral_edit_message(const PeerRef& receiver, std::int32_t msg_id, const std::string& text,
                                    const EphemeralMessageOptions& options) {
    require_session();
    if (msg_id <= 0) throw std::invalid_argument("ephemeral_edit_message needs a message id");
    check_ephemeral_options(text, options, true);
    fn::ephemeral_editMessage req;
    req.receiver_id = input_user_ptr(receiver);
    req.id = msg_id;
    if (!text.empty()) req.message = text;
    if (options.peer) req.peer = input_peer_ptr(*this, *options.peer);
    if (options.invert_media) req.invert_media = true;
    if (options.welcome) req.welcome = true;
    if (auto media = ephemeral_media_input(*this, options)) req.media = std::move(media);
    if (auto list = ephemeral_entities_of(options)) req.entities = std::move(*list);
    if (auto markup = ephemeral_markup_of(options)) req.reply_markup = std::move(*markup);
    if (options.rich_message) req.rich_message = rich_message_ptr(with_uploaded_rich_files(
        *this, *options.rich_message, [this](fn::messages_uploadMedia& up) { return call_rpc(up); }));
    (void)call_rpc(req);
    return true;
}

bool Client::ephemeral_send_message(const PeerRef& receiver, const std::string& text,
                                    const EphemeralMessageOptions& options) {
    require_session();
    check_ephemeral_options(text, options, false);
    fn::ephemeral_sendMessage req;
    req.receiver_id = input_user_ptr(receiver);
    req.message = text;
    req.random_id = detail::secure_random_i64();
    if (options.peer) req.peer = input_peer_ptr(*this, *options.peer);
    if (options.noforwards) req.noforwards = true;
    if (options.invert_media) req.invert_media = true;
    if (options.welcome) req.welcome = true;
    if (options.anchor) req.anchor = true;
    if (options.reply_to_msg_id) {
        ty::inputReplyToMessage reply;
        reply.reply_to_msg_id = *options.reply_to_msg_id;
        req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{reply});
    }
    if (auto list = ephemeral_entities_of(options)) req.entities = std::move(*list);
    if (auto media = ephemeral_media_input(*this, options)) req.media = std::move(media);
    if (auto markup = ephemeral_markup_of(options)) req.reply_markup = std::move(*markup);
    if (options.rich_message) req.rich_message = rich_message_ptr(with_uploaded_rich_files(
        *this, *options.rich_message, [this](fn::messages_uploadMedia& up) { return call_rpc(up); }));
    (void)call_rpc(req);
    return true;
}

bool Client::suggest_birthday(const PeerRef& user, const BirthdayInfo& birthday) {
    require_session();
    fn::users_suggestBirthday req;
    req.id = input_user_ptr(user);
    ty::birthday b;
    b.day = birthday.day;
    b.month = birthday.month;
    if (birthday.year) b.year = *birthday.year;
    req.birthday = std::make_shared<tl::Birthday>(tl::Birthday{b});
    (void)call_rpc(req);
    return true;
}

std::vector<TopPeerCategoryInfo> Client::get_top_peers(bool correspondents, bool bots_pm, bool bots_inline,
                                                       bool phone_calls, bool forward_users, bool forward_chats,
                                                       bool groups, bool channels, std::int32_t limit) {
    require_session();
    fn::contacts_getTopPeers req;
    if (correspondents) req.correspondents = true;
    if (bots_pm) req.bots_pm = true;
    if (bots_inline) req.bots_inline = true;
    if (phone_calls) req.phone_calls = true;
    if (forward_users) req.forward_users = true;
    if (forward_chats) req.forward_chats = true;
    if (groups) req.groups = true;
    if (channels) req.channels = true;
    req.offset = 0;
    req.limit = limit;
    req.hash = 0;
    const tl::contacts_TopPeers result = call_rpc(req);
    const auto* tp = std::get_if<ty::contacts_topPeers>(&result.value);
    if (tp == nullptr) return {};  // topPeersNotModified / topPeersDisabled carry no list
    cache_users_chats(tp->users, tp->chats);
    std::vector<TopPeerCategoryInfo> out;
    for (const auto& cat : tp->categories) {
        if (!cat) continue;
        const auto* c = std::get_if<ty::topPeerCategoryPeers>(&cat->value);
        if (c == nullptr || !c->category) continue;
        TopPeerCategoryInfo info;
        info.count = c->count;
        if (std::holds_alternative<ty::topPeerCategoryCorrespondents>(c->category->value)) {
            info.category = TopPeerCategory::Correspondents;
        } else if (std::holds_alternative<ty::topPeerCategoryBotsPM>(c->category->value)) {
            info.category = TopPeerCategory::BotsPM;
        } else if (std::holds_alternative<ty::topPeerCategoryBotsInline>(c->category->value)) {
            info.category = TopPeerCategory::BotsInline;
        } else if (std::holds_alternative<ty::topPeerCategoryPhoneCalls>(c->category->value)) {
            info.category = TopPeerCategory::PhoneCalls;
        } else if (std::holds_alternative<ty::topPeerCategoryForwardUsers>(c->category->value)) {
            info.category = TopPeerCategory::ForwardUsers;
        } else if (std::holds_alternative<ty::topPeerCategoryForwardChats>(c->category->value)) {
            info.category = TopPeerCategory::ForwardChats;
        } else if (std::holds_alternative<ty::topPeerCategoryGroups>(c->category->value)) {
            info.category = TopPeerCategory::Groups;
        } else if (std::holds_alternative<ty::topPeerCategoryChannels>(c->category->value)) {
            info.category = TopPeerCategory::Channels;
        } else if (std::holds_alternative<ty::topPeerCategoryBotsApp>(c->category->value)) {
            info.category = TopPeerCategory::BotsApp;
        } else if (std::holds_alternative<ty::topPeerCategoryBotsGuestChat>(c->category->value)) {
            info.category = TopPeerCategory::BotsGuestChat;
        } else {
            continue;
        }
        for (const auto& tp_peer : c->peers) {
            if (!tp_peer) continue;
            if (const auto* top = std::get_if<ty::topPeer>(&tp_peer->value)) {
                if (top->peer) info.peers.push_back(TopPeerInfo{detail::peer_from_tl(*top->peer, cache_), top->rating});
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

bool Client::change_sticker(const DocumentRef& sticker, const std::optional<std::string>& emoji,
                            const std::optional<std::string>& keywords) {
    require_session();
    fn::stickers_changeSticker req;
    req.sticker = input_document_ptr(sticker);
    if (emoji) req.emoji = *emoji;
    if (keywords) req.keywords = *keywords;
    (void)call_rpc(req);
    return true;
}

bool Client::set_sticker_set_thumb(const StickerSetRef& set, const std::optional<DocumentRef>& thumb) {
    require_session();
    fn::stickers_setStickerSetThumb req;
    req.stickerset = sticker_set_ptr(set);
    if (thumb) req.thumb = input_document_ptr(*thumb);
    (void)call_rpc(req);
    return true;
}

std::string Client::suggest_sticker_short_name(const std::string& title) {
    require_session();
    fn::stickers_suggestShortName req;
    req.title = title;
    const tl::stickers_SuggestedShortName result = call_rpc(req);
    const auto* s = std::get_if<ty::stickers_suggestedShortName>(&result.value);
    if (s == nullptr) throw std::runtime_error("unexpected stickers.SuggestedShortName variant");
    return s->short_name;
}

GroupCallParticipantsInfo Client::get_group_call_participants(const GroupCallRef& call, const std::vector<PeerRef>& ids,
                                                              const std::vector<std::int32_t>& sources,
                                                              const std::string& offset, std::int32_t limit) {
    require_session();
    fn::phone_getGroupParticipants req;
    ty::inputGroupCall c;
    c.id = call.id;
    c.access_hash = call.access_hash;
    req.call = std::make_shared<tl::InputGroupCall>(tl::InputGroupCall{c});
    for (const auto& p : ids) req.ids.push_back(input_peer_ptr(*this, p));
    req.sources = sources;
    req.offset = offset;
    req.limit = limit;
    const tl::phone_GroupParticipants result = call_rpc(req);
    const auto* gp = std::get_if<ty::phone_groupParticipants>(&result.value);
    if (gp == nullptr) throw std::runtime_error("unexpected phone.GroupParticipants variant");
    cache_users_chats(gp->users, gp->chats);
    GroupCallParticipantsInfo out;
    out.count = gp->count;
    out.version = gp->version;
    out.next_offset = gp->next_offset;
    for (const auto& part : gp->participants) {
        if (!part) continue;
        const auto* gpart = std::get_if<ty::groupCallParticipant>(&part->value);
        if (gpart == nullptr || !gpart->peer) continue;
        GroupCallParticipantInfo info;
        info.peer = detail::peer_from_tl(*gpart->peer, cache_);
        info.date = gpart->date;
        info.active_date = gpart->active_date;
        info.source = gpart->source;
        info.volume = gpart->volume;
        if (gpart->about) info.about = *gpart->about;
        info.muted = gpart->muted;
        info.left = gpart->left;
        info.can_self_unmute = gpart->can_self_unmute;
        info.just_joined = gpart->just_joined;
        info.muted_by_you = gpart->muted_by_you;
        info.volume_by_admin = gpart->volume_by_admin;
        info.self = gpart->self;
        info.video_joined = gpart->video_joined;
        info.has_video = gpart->video.has_value();
        info.has_presentation = gpart->presentation.has_value();
        info.raise_hand_rating = gpart->raise_hand_rating;
        info.paid_stars_total = gpart->paid_stars_total;
        out.participants.push_back(std::move(info));
    }
    return out;
}

bool Client::replace_sticker(const DocumentRef& sticker, const DocumentRef& new_document, const std::string& emoji) {
    require_session();
    if (emoji.empty()) throw std::invalid_argument("replace_sticker needs an emoji");
    fn::stickers_replaceSticker req;
    req.sticker = input_document_ptr(sticker);
    ty::inputStickerSetItem item;
    item.document = input_document_ptr(new_document);
    item.emoji = emoji;
    req.new_sticker = std::make_shared<tl::InputStickerSetItem>(tl::InputStickerSetItem{item});
    (void)call_rpc(req);
    return true;
}

}  // namespace cppgram
