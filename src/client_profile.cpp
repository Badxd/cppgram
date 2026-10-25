#include "cppgram/client.h"

#include <algorithm>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

ProfilePhoto to_profile_photo(const tl::Photo& p) {
    const auto* ph = std::get_if<ty::photo>(&p.value);
    ProfilePhoto out;
    if (ph == nullptr) return out;
    out.id = ph->id;
    out.access_hash = ph->access_hash;
    out.file_reference = ph->file_reference;
    out.date = ph->date;
    out.dc_id = ph->dc_id;
    out.has_video = ph->video_sizes.has_value() && !ph->video_sizes->empty();
    return out;
}

tl::InputPhoto input_photo_of(const ProfilePhoto& p) {
    return tl::InputPhoto{ty::inputPhoto{p.id, p.access_hash, p.file_reference}};
}

}  // namespace

std::vector<ProfilePhoto> Client::get_profile_photos(const PeerRef& user, int limit) {
    require_session();
    std::vector<ProfilePhoto> out;
    if (limit <= 0) return out;
    std::int64_t max_id = 0;
    while (static_cast<int>(out.size()) < limit) {
        fn::photos_getUserPhotos req;
        req.user_id = input_user_ptr(user);
        req.offset = 0;
        req.max_id = max_id;
        req.limit = std::min(100, limit - static_cast<int>(out.size()));
        const tl::photos_Photos result = session_->call(req);

        const std::vector<std::shared_ptr<tl::Photo>>* photos = nullptr;
        if (const auto* a = std::get_if<ty::photos_photos>(&result.value)) {
            photos = &a->photos;
        } else if (const auto* b = std::get_if<ty::photos_photosSlice>(&result.value)) {
            photos = &b->photos;
        }
        if (photos == nullptr || photos->empty()) break;

        std::size_t added = 0;
        for (const auto& p : *photos) {
            if (!p) continue;
            ProfilePhoto pp = to_profile_photo(*p);
            if (pp.id == 0) continue;
            if (static_cast<int>(out.size()) >= limit) break;
            max_id = pp.id;
            out.push_back(std::move(pp));
            ++added;
        }
        if (added == 0) break;
    }
    return out;
}

ProfilePhoto Client::set_profile_photo(const std::string& path) {
    require_session();
    std::string name;
    std::int64_t size = 0;
    fn::photos_uploadProfilePhoto req;
    req.file = upload(path, name, size);
    const tl::photos_Photo result = session_->call(req);
    const auto* wrap = std::get_if<ty::photos_photo>(&result.value);
    if (wrap == nullptr || !wrap->photo) {
        throw std::runtime_error("photos.uploadProfilePhoto returned no photo");
    }
    return to_profile_photo(*wrap->photo);
}

ProfilePhoto Client::set_profile_photo_from(const ProfilePhoto& photo) {
    require_session();
    fn::photos_updateProfilePhoto req;
    req.id = std::make_shared<tl::InputPhoto>(input_photo_of(photo));
    const tl::photos_Photo result = session_->call(req);
    const auto* wrap = std::get_if<ty::photos_photo>(&result.value);
    if (wrap == nullptr || !wrap->photo) {
        throw std::runtime_error("photos.updateProfilePhoto returned no photo");
    }
    return to_profile_photo(*wrap->photo);
}

std::size_t Client::delete_profile_photos(const std::vector<ProfilePhoto>& photos) {
    require_session();
    if (photos.empty()) return 0;
    fn::photos_deletePhotos req;
    for (const auto& p : photos) {
        req.id.push_back(std::make_shared<tl::InputPhoto>(input_photo_of(p)));
    }
    const std::vector<std::int64_t> removed = session_->call(req);
    return removed.size();
}

std::vector<Chat> Client::get_common_chats(const PeerRef& user, int limit) {
    require_session();
    std::vector<Chat> out;
    if (limit <= 0) return out;
    std::int64_t max_id = 0;
    while (static_cast<int>(out.size()) < limit) {
        fn::messages_getCommonChats req;
        req.user_id = input_user_ptr(user);
        req.max_id = max_id;
        req.limit = std::min(100, limit - static_cast<int>(out.size()));
        const tl::messages_Chats result = session_->call(req);

        const std::vector<std::shared_ptr<tl::Chat>>* chats = nullptr;
        if (const auto* a = std::get_if<ty::messages_chats>(&result.value)) {
            chats = &a->chats;
        } else if (const auto* b = std::get_if<ty::messages_chatsSlice>(&result.value)) {
            chats = &b->chats;
        }
        if (chats == nullptr || chats->empty()) break;

        std::size_t added = 0;
        for (const auto& c : *chats) {
            if (!c) continue;
            auto chat = detail::chat_from_tl(*c);
            if (!chat) continue;
            if (static_cast<int>(out.size()) >= limit) break;
            max_id = chat->id;
            out.push_back(std::move(*chat));
            ++added;
        }
        if (added == 0) break;
    }
    return out;
}

std::vector<SessionInfo> Client::get_sessions() {
    require_session();
    fn::account_getAuthorizations req;
    const tl::account_Authorizations result = session_->call(req);
    std::vector<SessionInfo> out;
    const auto* list = std::get_if<ty::account_authorizations>(&result.value);
    if (list == nullptr) return out;
    for (const auto& a : list->authorizations) {
        if (!a) continue;
        const auto* d = std::get_if<ty::authorization>(&a->value);
        if (d == nullptr) continue;
        SessionInfo s;
        s.hash = d->hash;
        s.device_model = d->device_model;
        s.platform = d->platform;
        s.system_version = d->system_version;
        s.app_name = d->app_name;
        s.app_version = d->app_version;
        s.api_id = d->api_id;
        s.date_created = d->date_created;
        s.date_active = d->date_active;
        s.ip = d->ip;
        s.country = d->country;
        s.region = d->region;
        s.current = d->current;
        s.official_app = d->official_app;
        s.password_pending = d->password_pending;
        s.unconfirmed = d->unconfirmed;
        out.push_back(std::move(s));
    }
    return out;
}

bool Client::terminate_session(std::int64_t hash) {
    require_session();
    fn::account_resetAuthorization req;
    req.hash = hash;
    return session_->call(req);
}

}  // namespace cppgram
