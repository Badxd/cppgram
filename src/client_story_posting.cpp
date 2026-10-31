#include "cppgram/client.h"
#include "secure_random.h"

#include <memory>
#include <random>
#include <stdexcept>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::int64_t story_random_id() {
    return detail::secure_random_i64();
}

}  // namespace

std::int32_t Client::post_story(const std::string& path, bool as_photo, const std::string& caption,
                                std::int32_t period, bool pinned, bool noforwards) {
    require_session();
    const UploadedFile file = save_file(path);
    if (!file.handle) throw std::runtime_error("post_story: upload returned no handle");

    std::shared_ptr<tl::InputMedia> media;
    if (as_photo) {
        ty::inputMediaUploadedPhoto photo;
        photo.file = file.handle;
        media = std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
    } else {
        ty::inputMediaUploadedDocument doc;
        doc.file = file.handle;
        doc.mime_type = "video/mp4";
        doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(
            tl::DocumentAttribute{ty::documentAttributeFilename{file.name}}));
        media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
    }

    fn::stories_sendStory req;
    req.pinned = pinned;
    req.noforwards = noforwards;
    req.peer = std::make_shared<tl::InputPeer>(tl::InputPeer{ty::inputPeerSelf{}});
    req.media = media;
    if (!caption.empty()) {
        ty::textWithEntities t;
        t.text = caption;
        req.caption = t.text;
    }
    req.privacy_rules.push_back(std::make_shared<tl::InputPrivacyRule>(
        tl::InputPrivacyRule{ty::inputPrivacyValueAllowAll{}}));
    req.random_id = story_random_id();
    req.period = period;

    const tl::Updates updates = session_->call(req);
    for (const auto& update : std::get_if<ty::updates>(&updates.value) ? std::get<ty::updates>(updates.value).updates
                                                                        : std::vector<std::shared_ptr<tl::Update>>{}) {
        if (!update) continue;
        if (const auto* story = std::get_if<ty::updateStory>(&update->value)) {
            if (story->story) {
                if (const auto* info = std::get_if<ty::storyItem>(&story->story->value)) return info->id;
            }
        }
    }
    return 0;
}

}  // namespace cppgram
