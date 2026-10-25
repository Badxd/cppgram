#include "cppgram/client.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t random_i64() {
    const mtproto::Bytes rnd = mtproto::random_bytes(8);
    return static_cast<std::int64_t>(mtproto::le_u64_from(rnd));
}

std::string lower_ext(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_photo_path(const std::string& path) {
    const std::string ext = lower_ext(path);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

std::string document_mime(const std::string& path) {
    const std::string ext = lower_ext(path);
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".gif") return "image/gif";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ogg" || ext == ".oga") return "audio/ogg";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".zip") return "application/zip";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

}  // namespace

Message Client::send_quote(const PeerRef& target, const std::string& text, std::int32_t reply_to_id,
                           const std::string& quote_text) {
    require_session();
    if (quote_text.empty()) throw std::invalid_argument("send_quote: quote_text must not be empty");
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = text;
    req.random_id = random_i64();
    ty::inputReplyToMessage reply;
    reply.reply_to_msg_id = reply_to_id;
    reply.quote_text = quote_text;
    req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{reply});
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, text, 0)) return *msg;
    throw std::runtime_error("quoted message was sent but no message was returned by the server");
}

void Client::send_scheduled(const PeerRef& target, const std::string& text, std::int64_t unix_time) {
    require_session();
    if (unix_time <= 0) throw std::invalid_argument("send_scheduled: unix_time must be a positive epoch second");
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = text;
    req.random_id = random_i64();
    req.schedule_date = static_cast<std::int32_t>(unix_time);
    session_->call(req);
}

std::vector<Message> Client::send_album(const PeerRef& target, const std::vector<std::string>& paths,
                                        const std::string& caption) {
    require_session();
    if (paths.size() < 2 || paths.size() > 10) {
        throw std::invalid_argument("send_album: an album needs between 2 and 10 files");
    }
    fn::messages_sendMultiMedia req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    for (std::size_t i = 0; i < paths.size(); ++i) {
        std::string name;
        std::int64_t size = 0;
        auto file = upload(paths[i], name, size);

        // Albums need media that is already stored on Telegram, so each file goes through messages.uploadMedia first.
        fn::messages_uploadMedia up;
        up.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
        if (is_photo_path(name)) {
            ty::inputMediaUploadedPhoto photo;
            photo.file = file;
            up.media = std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
        } else {
            ty::inputMediaUploadedDocument doc;
            doc.file = file;
            doc.mime_type = document_mime(name);
            doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
            up.media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
        }
        const tl::MessageMedia uploaded = session_->call(up);

        std::shared_ptr<tl::InputMedia> ref;
        if (const auto* mp = std::get_if<ty::messageMediaPhoto>(&uploaded.value)) {
            const auto* photo = mp->photo ? std::get_if<ty::photo>(&(*mp->photo)->value) : nullptr;
            if (photo == nullptr) throw std::runtime_error("uploadMedia returned no photo for " + paths[i]);
            ty::inputMediaPhoto in;
            in.id = std::make_shared<tl::InputPhoto>(tl::InputPhoto{ty::inputPhoto{photo->id, photo->access_hash, photo->file_reference}});
            ref = std::make_shared<tl::InputMedia>(tl::InputMedia{in});
        } else if (const auto* md = std::get_if<ty::messageMediaDocument>(&uploaded.value)) {
            const auto* doc = md->document ? std::get_if<ty::document>(&(*md->document)->value) : nullptr;
            if (doc == nullptr) throw std::runtime_error("uploadMedia returned no document for " + paths[i]);
            ty::inputMediaDocument in;
            in.id = std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{doc->id, doc->access_hash, doc->file_reference}});
            ref = std::make_shared<tl::InputMedia>(tl::InputMedia{in});
        } else {
            throw std::runtime_error("uploadMedia returned an unexpected media type for " + paths[i]);
        }

        ty::inputSingleMedia single;
        single.media = ref;
        single.random_id = random_i64();
        if (i == 0) single.message = caption;
        req.multi_media.push_back(std::make_shared<tl::InputSingleMedia>(tl::InputSingleMedia{single}));
    }

    const tl::Updates updates = session_->call(req);
    std::vector<Message> out;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        for (const auto& up : u->updates) {
            if (!up) continue;
            if (const auto* nm = std::get_if<ty::updateNewMessage>(&up->value)) {
                if (nm->message) if (auto msg = detail::message_from_tl(*nm->message, cache_)) out.push_back(*msg);
            } else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) {
                if (ncm->message) if (auto msg = detail::message_from_tl(*ncm->message, cache_)) out.push_back(*msg);
            }
        }
    }
    if (out.empty()) throw std::runtime_error("album was sent but no messages were returned by the server");
    return out;
}

std::string Client::translate_text(const std::string& text, const std::string& to_lang) {
    require_session();
    fn::messages_translateText req;
    ty::textWithEntities src;
    src.text = text;
    req.text = std::vector<std::shared_ptr<tl::TextWithEntities>>{
        std::make_shared<tl::TextWithEntities>(tl::TextWithEntities{src})};
    req.to_lang = to_lang;
    const tl::messages_TranslatedText result = session_->call(req);
    const auto* list = std::get_if<ty::messages_translateResult>(&result.value);
    if (list == nullptr || list->result.empty() || !list->result.front()) {
        throw std::runtime_error("messages.translateText returned no text");
    }
    const auto* t = std::get_if<ty::textWithEntities>(&list->result.front()->value);
    if (t == nullptr) throw std::runtime_error("messages.translateText returned an unexpected entity");
    return t->text;
}

void Client::set_online(bool online) {
    require_session();
    fn::account_updateStatus req;
    req.offline = !online;
    session_->call(req);
}

}  // namespace cppgram

namespace cppgram {

Message Client::send_uploaded(const PeerRef& target, const UploadedFile& file, bool as_photo,
                              const std::string& caption) {
    require_session();
    if (!file.handle) throw std::invalid_argument("send_uploaded: the UploadedFile has no handle");
    std::shared_ptr<tl::InputMedia> media;
    if (as_photo) {
        ty::inputMediaUploadedPhoto photo;
        photo.file = file.handle;
        media = std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
    } else {
        ty::inputMediaUploadedDocument doc;
        doc.file = file.handle;
        doc.mime_type = document_mime(file.name);
        doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{file.name}}));
        media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
    }
    return send_media(target, media, caption);
}

}  // namespace cppgram
