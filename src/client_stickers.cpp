#include "cppgram/client.h"

#include <filesystem>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

using mtproto::Bytes;
namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputStickerSet> short_name_set(const std::string& short_name) {
    if (short_name.empty()) throw std::invalid_argument("sticker set short name is empty");
    return std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetShortName{short_name}});
}

std::shared_ptr<tl::InputDocument> input_document_of(const MediaInfo& m) {
    if (m.kind != MediaKind::Document) throw std::invalid_argument("expected a sticker document");
    return std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{m.id, m.access_hash, m.file_reference}});
}

// Converts a Telegram document (sticker or otherwise) into MediaInfo.
std::optional<MediaInfo> media_info_of(const tl::Document& doc) {
    const auto* d = std::get_if<ty::document>(&doc.value);
    if (d == nullptr) return std::nullopt;
    MediaInfo info;
    info.kind = MediaKind::Document;
    info.id = d->id;
    info.access_hash = d->access_hash;
    info.file_reference = d->file_reference;
    info.mime_type = d->mime_type;
    info.size = d->size;
    info.dc_id = d->dc_id;
    for (const auto& attr : d->attributes) {
        if (!attr) continue;
        if (std::holds_alternative<ty::documentAttributeSticker>(attr->value)) info.is_sticker = true;
        if (const auto* fn_attr = std::get_if<ty::documentAttributeFilename>(&attr->value)) info.file_name = fn_attr->file_name;
    }
    return info;
}

StickerSetInfo info_of(const ty::stickerSet& s) {
    StickerSetInfo info;
    info.id = s.id;
    info.access_hash = s.access_hash;
    info.title = s.title;
    info.short_name = s.short_name;
    info.count = s.count;
    return info;
}

std::string sticker_mime(const std::string& path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".webp") return "image/webp";
    if (ext == ".png") return "image/png";
    if (ext == ".tgs") return "application/x-tgsticker";
    if (ext == ".webm") return "video/webm";
    throw std::invalid_argument("unsupported sticker file type: " + ext);
}

}  // namespace

Chat Client::get_chat(const PeerRef& peer) {
    require_session();
    Chat out;
    out.id = peer.id;
    out.access_hash = peer.access_hash;
    out.kind = peer.kind;
    if (peer.kind == PeerKind::Channel) {
        fn::channels_getFullChannel req;
        req.channel = std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{peer.id, peer.access_hash}});
        const tl::messages_ChatFull full = session_->call(req);
        const auto* mcf = std::get_if<ty::messages_chatFull>(&full.value);
        if (mcf == nullptr || !mcf->full_chat) throw std::runtime_error("channels.getFullChannel returned no data");
        cache_users_chats(mcf->users, mcf->chats);
        for (const auto& c : mcf->chats) {
            if (!c) continue;
            if (auto chat = detail::chat_from_tl(*c)) {
                if (chat->id == peer.id) out = *chat;
            }
        }
        if (const auto* cf = std::get_if<ty::channelFull>(&mcf->full_chat->value)) {
            out.about = cf->about;
            out.member_count = cf->participants_count.value_or(0);
        }
        return out;
    }
    if (peer.kind == PeerKind::Chat) {
        fn::messages_getFullChat req;
        req.chat_id = peer.id;
        const tl::messages_ChatFull full = session_->call(req);
        const auto* mcf = std::get_if<ty::messages_chatFull>(&full.value);
        if (mcf == nullptr || !mcf->full_chat) throw std::runtime_error("messages.getFullChat returned no data");
        cache_users_chats(mcf->users, mcf->chats);
        for (const auto& c : mcf->chats) {
            if (!c) continue;
            if (auto chat = detail::chat_from_tl(*c)) {
                if (chat->id == peer.id) out = *chat;
            }
        }
        if (const auto* cf = std::get_if<ty::chatFull>(&mcf->full_chat->value)) {
            out.about = cf->about;
            if (cf->participants) {
                if (const auto* cps = std::get_if<ty::chatParticipants>(&cf->participants->value)) {
                    out.member_count = static_cast<std::int32_t>(cps->participants.size());
                }
            }
        }
        return out;
    }
    if (peer.kind == PeerKind::User) {
        fn::users_getFullUser req;
        req.id = input_user_ptr(peer);
        const tl::users_UserFull full = session_->call(req);
        const auto* uf = std::get_if<ty::users_userFull>(&full.value);
        if (uf == nullptr) throw std::runtime_error("users.getFullUser returned no data");
        cache_users_chats(uf->users, uf->chats);
        for (const auto& u : uf->users) {
            if (!u) continue;
            if (auto user = detail::user_from_tl(*u)) {
                if (user->id == peer.id) {
                    out.title = (user->first_name + (user->last_name.empty() ? "" : " " + user->last_name));
                    out.username = user->username;
                }
            }
        }
        out.type = ChatType::Private;
        if (uf->full_user) {
            if (const auto* fu = std::get_if<ty::userFull>(&uf->full_user->value)) out.about = fu->about.value_or("");
        }
        return out;
    }
    throw std::invalid_argument("get_chat: unsupported peer kind");
}

StickerSetInfo Client::create_sticker_set(const std::string& title, const std::string& short_name,
                                          const std::vector<StickerFile>& stickers) {
    require_session();
    if (stickers.empty()) throw std::invalid_argument("a sticker set needs at least one sticker");
    fn::stickers_createStickerSet req;
    req.user_id = std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}});
    req.title = title;
    req.short_name = short_name;
    for (const auto& sf : stickers) {
        std::string name;
        std::int64_t size = 0;
        auto file = upload(sf.path, name, size);
        ty::inputMediaUploadedDocument doc;
        doc.file = file;
        doc.mime_type = sticker_mime(sf.path);
        ty::documentAttributeSticker attr;
        attr.alt = sf.emoji;
        attr.stickerset = std::make_shared<tl::InputStickerSet>(tl::InputStickerSet{ty::inputStickerSetEmpty{}});
        doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{attr}));
        doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));

        // messages.uploadMedia stores the file on Telegram and returns its document reference.
        fn::messages_uploadMedia up;
        up.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
        up.media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
        const tl::MessageMedia uploaded = session_->call(up);
        const auto* md = std::get_if<ty::messageMediaDocument>(&uploaded.value);
        if (md == nullptr || !md->document || !*md->document) {
            throw std::runtime_error("messages.uploadMedia did not return a document for " + sf.path);
        }
        const auto* d = std::get_if<ty::document>(&(*md->document)->value);
        if (d == nullptr) throw std::runtime_error("uploaded sticker has no document data");

        ty::inputStickerSetItem item;
        item.document = std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{d->id, d->access_hash, d->file_reference}});
        item.emoji = sf.emoji;
        req.stickers.push_back(std::make_shared<tl::InputStickerSetItem>(tl::InputStickerSetItem{item}));
    }
    const tl::messages_StickerSet result = session_->call(req);
    const auto* set = std::get_if<ty::messages_stickerSet>(&result.value);
    if (set == nullptr || !set->set) throw std::runtime_error("stickers.createStickerSet returned no set");
    const auto* meta = std::get_if<ty::stickerSet>(&set->set->value);
    if (meta == nullptr) throw std::runtime_error("unexpected sticker set type");
    return info_of(*meta);
}

StickerSetContent Client::get_sticker_set(const std::string& short_name) {
    require_session();
    fn::messages_getStickerSet req;
    req.stickerset = short_name_set(short_name);
    req.hash = 0;
    const tl::messages_StickerSet result = session_->call(req);
    const auto* set = std::get_if<ty::messages_stickerSet>(&result.value);
    if (set == nullptr || !set->set) throw std::runtime_error("sticker set not found: " + short_name);
    const auto* meta = std::get_if<ty::stickerSet>(&set->set->value);
    if (meta == nullptr) throw std::runtime_error("unexpected sticker set type");
    StickerSetContent out;
    out.info = info_of(*meta);
    for (const auto& d : set->documents) {
        if (!d) continue;
        if (auto m = media_info_of(*d)) out.stickers.push_back(*m);
    }
    return out;
}

std::vector<StickerSetInfo> Client::get_installed_sticker_sets() {
    require_session();
    fn::messages_getAllStickers req;
    req.hash = 0;
    const tl::messages_AllStickers result = session_->call(req);
    std::vector<StickerSetInfo> out;
    if (const auto* all = std::get_if<ty::messages_allStickers>(&result.value)) {
        for (const auto& s : all->sets) {
            if (!s) continue;
            if (const auto* meta = std::get_if<ty::stickerSet>(&s->value)) out.push_back(info_of(*meta));
        }
    }
    return out;
}

std::vector<MediaInfo> Client::get_stickers_by_emoji(const std::string& emoji) {
    require_session();
    fn::messages_getStickers req;
    req.emoticon = emoji;
    req.hash = 0;
    const tl::messages_Stickers result = session_->call(req);
    std::vector<MediaInfo> out;
    if (const auto* st = std::get_if<ty::messages_stickers>(&result.value)) {
        for (const auto& d : st->stickers) {
            if (!d) continue;
            if (auto m = media_info_of(*d)) out.push_back(*m);
        }
    }
    return out;
}

void Client::delete_sticker_set(const std::string& short_name) {
    require_session();
    fn::stickers_deleteStickerSet req;
    req.stickerset = short_name_set(short_name);
    session_->call(req);
}

void Client::rename_sticker_set(const std::string& short_name, const std::string& title) {
    require_session();
    fn::stickers_renameStickerSet req;
    req.stickerset = short_name_set(short_name);
    req.title = title;
    session_->call(req);
}

void Client::remove_sticker_from_set(const MediaInfo& sticker) {
    require_session();
    fn::stickers_removeStickerFromSet req;
    req.sticker = input_document_of(sticker);
    session_->call(req);
}

void Client::add_existing_sticker_to_set(const std::string& short_name, const MediaInfo& sticker,
                                         const std::string& emoji) {
    require_session();
    fn::stickers_addStickerToSet req;
    req.stickerset = short_name_set(short_name);
    ty::inputStickerSetItem item;
    item.document = input_document_of(sticker);
    item.emoji = emoji;
    req.sticker = std::make_shared<tl::InputStickerSetItem>(tl::InputStickerSetItem{item});
    session_->call(req);
}

void Client::change_sticker_position(const MediaInfo& sticker, std::int32_t position) {
    require_session();
    fn::stickers_changeStickerPosition req;
    req.sticker = input_document_of(sticker);
    req.position = position;
    session_->call(req);
}

}  // namespace cppgram
