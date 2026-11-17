#include "cppgram/client.h"

#include <cctype>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

StoryInfo to_story_info(const tl::StoryItem& item) {
    const auto* s = std::get_if<ty::storyItem>(&item.value);
    if (s == nullptr) {
        throw std::runtime_error("story is deleted or skipped");
    }
    StoryInfo out;
    out.id = s->id;
    out.date = s->date;
    out.expire_date = s->expire_date;
    out.pinned = s->pinned;
    out.noforwards = s->noforwards;
    out.caption = s->caption.value_or("");
    return out;
}

// Keeps only the stories that are full items. Deleted or skipped entries are dropped.
std::vector<StoryInfo> to_story_list(const tl::stories_Stories& res) {
    std::vector<StoryInfo> out;
    const auto* page = std::get_if<ty::stories_stories>(&res.value);
    if (page == nullptr) return out;
    for (const auto& item : page->stories) {
        if (!item) continue;
        if (std::get_if<ty::storyItem>(&item->value) == nullptr) continue;
        out.push_back(to_story_info(*item));
    }
    return out;
}

}  // namespace

// ---- phase 3: messages extras

std::vector<MessageReadParticipant> Client::get_message_read_participants(const PeerRef& chat,
                                                                          std::int32_t message_id) {
    require_session();
    fn::messages_getMessageReadParticipants req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    const auto res = call_rpc(req);
    std::vector<MessageReadParticipant> out;
    for (const auto& p : res) {
        if (!p) continue;
        const auto* rp = std::get_if<ty::readParticipantDate>(&p->value);
        if (rp == nullptr) continue;
        out.push_back(MessageReadParticipant{rp->user_id, rp->date});
    }
    return out;
}

void Client::delete_scheduled_messages(const PeerRef& chat, const std::vector<std::int32_t>& message_ids) {
    require_session();
    fn::messages_deleteScheduledMessages req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = message_ids;
    call_rpc(req);
}

std::vector<AvailableEffectInfo> Client::get_available_effects() {
    require_session();
    fn::messages_getAvailableEffects req;
    req.hash = 0;
    const tl::messages_AvailableEffects res = call_rpc(req);
    std::vector<AvailableEffectInfo> out;
    const auto* list = std::get_if<ty::messages_availableEffects>(&res.value);
    if (list == nullptr) return out;
    for (const auto& e : list->effects) {
        if (!e) continue;
        const auto* eff = std::get_if<ty::availableEffect>(&e->value);
        if (eff == nullptr) continue;
        AvailableEffectInfo info;
        info.id = eff->id;
        info.emoticon = eff->emoticon;
        info.premium_required = eff->premium_required;
        out.push_back(info);
    }
    return out;
}

void Client::send_reaction_with_options(const PeerRef& chat, std::int32_t message_id, const std::string& emoji,
                                        bool big, bool add_to_recent) {
    require_session();
    fn::messages_sendReaction req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    req.big = big;
    req.add_to_recent = add_to_recent;
    std::vector<std::shared_ptr<tl::Reaction>> reactions;
    reactions.push_back(std::make_shared<tl::Reaction>(tl::Reaction{ty::reactionEmoji{emoji}}));
    req.reaction = std::move(reactions);
    call_rpc(req);
}

void Client::clear_reaction(const PeerRef& chat, std::int32_t message_id) {
    require_session();
    fn::messages_sendReaction req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    req.reaction = std::vector<std::shared_ptr<tl::Reaction>>{};
    call_rpc(req);
}

// ---- phase 4: stories

std::vector<StoryInfo> Client::get_pinned_stories(const PeerRef& chat, std::int32_t offset_id, std::int32_t limit) {
    require_session();
    fn::stories_getPinnedStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset_id = offset_id;
    req.limit = limit;
    return to_story_list(call_rpc(req));
}

std::vector<StoryInfo> Client::get_stories_archive(const PeerRef& chat, std::int32_t offset_id, std::int32_t limit) {
    require_session();
    fn::stories_getStoriesArchive req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset_id = offset_id;
    req.limit = limit;
    return to_story_list(call_rpc(req));
}

std::vector<StoryInfo> Client::get_stories_by_id(const PeerRef& chat, const std::vector<std::int32_t>& story_ids) {
    require_session();
    fn::stories_getStoriesByID req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    return to_story_list(call_rpc(req));
}

std::vector<std::int32_t> Client::delete_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids) {
    require_session();
    fn::stories_deleteStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    return call_rpc(req);
}

std::vector<std::int32_t> Client::toggle_pinned_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids,
                                                        bool pinned) {
    require_session();
    fn::stories_togglePinned req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    req.pinned = pinned;
    return call_rpc(req);
}

std::string Client::export_story_link(const PeerRef& chat, std::int32_t story_id) {
    require_session();
    fn::stories_exportStoryLink req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_id;
    const tl::ExportedStoryLink res = call_rpc(req);
    const auto* link = std::get_if<ty::exportedStoryLink>(&res.value);
    if (link == nullptr) throw std::runtime_error("export_story_link: no link returned");
    return link->link;
}

// ---- stories part 3

std::int32_t Client::can_send_story_count(const PeerRef& chat) {
    require_session();
    fn::stories_canSendStory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const auto res = call_rpc(req);
    const auto* count = std::get_if<ty::stories_canSendStoryCount>(&res.value);
    if (count == nullptr) throw std::runtime_error("unexpected stories.CanSendStoryCount variant");
    return count->count_remains;
}

bool Client::increment_story_views(const PeerRef& chat, const std::vector<std::int32_t>& story_ids) {
    require_session();
    if (story_ids.empty()) throw std::invalid_argument("increment_story_views needs at least one story id");
    fn::stories_incrementStoryViews req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    return call_rpc(req);
}

std::vector<StoryInfo> Client::get_peer_stories(const PeerRef& chat) {
    require_session();
    fn::stories_getPeerStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    const auto res = call_rpc(req);
    const auto* ps = std::get_if<ty::stories_peerStories>(&res.value);
    if (ps == nullptr || !ps->stories) throw std::runtime_error("unexpected stories.PeerStories variant");
    const auto* peer = std::get_if<ty::peerStories>(&ps->stories->value);
    if (peer == nullptr) throw std::runtime_error("unexpected PeerStories variant");
    std::vector<StoryInfo> out;
    for (const auto& item : peer->stories) {
        if (!item || std::get_if<ty::storyItem>(&item->value) == nullptr) continue;
        out.push_back(to_story_info(*item));
    }
    return out;
}

StoryViewsPage Client::get_story_views(const PeerRef& chat, std::int32_t story_id, const std::string& offset,
                                       std::int32_t limit, bool just_contacts) {
    require_session();
    if (limit <= 0) throw std::invalid_argument("get_story_views limit must be positive");
    fn::stories_getStoryViewsList req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_id;
    req.offset = offset;
    req.limit = limit;
    req.just_contacts = just_contacts;
    const auto res = call_rpc(req);
    const auto* list = std::get_if<ty::stories_storyViewsList>(&res.value);
    if (list == nullptr) throw std::runtime_error("unexpected stories.StoryViewsList variant");
    StoryViewsPage page;
    page.views_count = list->views_count;
    page.forwards_count = list->forwards_count;
    page.reactions_count = list->reactions_count;
    page.next_offset = list->next_offset.value_or("");
    for (const auto& v : list->views) {
        if (!v) continue;
        const auto* sv = std::get_if<ty::storyView>(&v->value);
        if (sv == nullptr) continue;  // public forwards and reposts carry no user id here
        page.views.push_back(StoryViewerInfo{sv->user_id, sv->date, sv->blocked});
    }
    return page;
}

AllStoriesInfo Client::get_all_stories(bool next, bool hidden, const std::string& state) {
    require_session();
    fn::stories_getAllStories req;
    if (next) req.next = true;
    if (hidden) req.hidden = true;
    if (!state.empty()) req.state = state;
    const auto res = call_rpc(req);
    AllStoriesInfo out;
    if (const auto* nm = std::get_if<ty::stories_allStoriesNotModified>(&res.value)) {
        out.not_modified = true;
        out.state = nm->state;
        return out;
    }
    const auto* all = std::get_if<ty::stories_allStories>(&res.value);
    if (all == nullptr) throw std::runtime_error("unexpected stories.AllStories variant");
    out.has_more = all->has_more;
    out.count = all->count;
    out.state = all->state;
    cache_users_chats(all->users, all->chats);
    for (const auto& ps : all->peer_stories) {
        if (!ps) continue;
        const auto* peer_stories = std::get_if<ty::peerStories>(&ps->value);
        if (peer_stories == nullptr || !peer_stories->peer) continue;
        PeerStoriesInfo group;
        group.peer = detail::peer_from_tl(*peer_stories->peer, cache_);
        for (const auto& item : peer_stories->stories) {
            if (!item || std::get_if<ty::storyItem>(&item->value) == nullptr) continue;
            group.stories.push_back(to_story_info(*item));
        }
        out.peers.push_back(std::move(group));
    }
    return out;
}

namespace {

void check_percent(double v, const char* name) {
    if (!(v >= 0.0 && v <= 100.0)) {
        throw std::invalid_argument(std::string("story media area ") + name + " must be between 0 and 100");
    }
}

// Builds one MediaArea for stories.editStory. Throws std::invalid_argument on missing or out-of-range fields.
tl::MediaArea build_media_area(const StoryMediaArea& area) {
    check_percent(area.x, "x");
    check_percent(area.y, "y");
    check_percent(area.w, "w");
    check_percent(area.h, "h");
    ty::mediaAreaCoordinates coords;
    coords.x = area.x;
    coords.y = area.y;
    coords.w = area.w;
    coords.h = area.h;
    coords.rotation = area.rotation;
    const auto coordinates = std::make_shared<tl::MediaAreaCoordinates>(tl::MediaAreaCoordinates{coords});

    switch (area.kind) {
        case StoryMediaAreaKind::Url: {
            if (area.url.empty()) throw std::invalid_argument("story url media area needs a url");
            ty::mediaAreaUrl m;
            m.coordinates = coordinates;
            m.url = area.url;
            return tl::MediaArea{m};
        }
        case StoryMediaAreaKind::Weather: {
            if (area.emoji.empty()) throw std::invalid_argument("story weather media area needs an emoji");
            ty::mediaAreaWeather m;
            m.coordinates = coordinates;
            m.emoji = area.emoji;
            m.temperature_c = area.temperature_c;
            m.color = area.color;
            return tl::MediaArea{m};
        }
        case StoryMediaAreaKind::StarGift: {
            if (area.gift_slug.empty()) throw std::invalid_argument("story star gift media area needs a slug");
            ty::mediaAreaStarGift m;
            m.coordinates = coordinates;
            m.slug = area.gift_slug;
            return tl::MediaArea{m};
        }
        case StoryMediaAreaKind::Location: {
            if (area.latitude < -90.0 || area.latitude > 90.0 || area.longitude < -180.0 || area.longitude > 180.0) {
                throw std::invalid_argument("story location media area has coordinates out of range");
            }
            ty::geoPoint geo;
            geo.long_ = area.longitude;
            geo.lat = area.latitude;
            geo.access_hash = 0;
            ty::mediaAreaGeoPoint m;
            m.coordinates = coordinates;
            m.geo = std::make_shared<tl::GeoPoint>(tl::GeoPoint{geo});
            return tl::MediaArea{m};
        }
    }
    throw std::invalid_argument("unknown story media area kind");
}

std::shared_ptr<tl::InputPrivacyRule> build_privacy_rule(const StoryPrivacyRule& rule) {
    const auto make = [](auto value) {
        return std::make_shared<tl::InputPrivacyRule>(tl::InputPrivacyRule{std::move(value)});
    };
    switch (rule.kind) {
        case StoryPrivacyKind::Everyone:
            return make(ty::inputPrivacyValueAllowAll{});
        case StoryPrivacyKind::Contacts:
            return make(ty::inputPrivacyValueAllowContacts{});
        case StoryPrivacyKind::CloseFriends:
            return make(ty::inputPrivacyValueAllowCloseFriends{});
        case StoryPrivacyKind::Nobody:
            return make(ty::inputPrivacyValueDisallowAll{});
        case StoryPrivacyKind::AllowUsers:
        case StoryPrivacyKind::DisallowUsers: {
            std::vector<std::shared_ptr<tl::InputUser>> users;
            for (const auto& peer : rule.users) {
                if (peer.kind != PeerKind::User) {
                    throw std::invalid_argument("story privacy rule users must be user peers");
                }
                if (!peer.has_access_hash) {
                    throw std::invalid_argument("story privacy rule user needs an access hash");
                }
                users.push_back(std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUser{peer.id, peer.access_hash}}));
            }
            if (rule.kind == StoryPrivacyKind::AllowUsers) {
                ty::inputPrivacyValueAllowUsers v;
                v.users = std::move(users);
                return make(std::move(v));
            }
            ty::inputPrivacyValueDisallowUsers v;
            v.users = std::move(users);
            return make(std::move(v));
        }
    }
    throw std::invalid_argument("unknown story privacy kind");
}

// Video MIME type from the file extension. Anything not recognised is sent as mp4.
std::string video_mime_for(const std::string& name) {
    const auto dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? std::string{} : name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "mov") return "video/quicktime";
    if (ext == "webm") return "video/webm";
    return "video/mp4";
}

}  // namespace

bool Client::edit_story_caption(const PeerRef& chat, std::int32_t story_id, const std::string& caption) {
    StoryEditOptions options;
    options.caption = caption;
    return edit_story(chat, story_id, options);
}

bool Client::edit_story(const PeerRef& chat, std::int32_t story_id, const StoryEditOptions& options) {
    require_session();
    const bool has_media = !options.media_path.empty();
    if (!has_media && !options.caption && !options.media_areas && !options.privacy_rules && !options.music) {
        throw std::invalid_argument("edit_story: nothing to change");
    }
    if (options.privacy_rules && options.privacy_rules->empty()) {
        throw std::invalid_argument("edit_story: privacy_rules must not be empty");
    }

    fn::stories_editStory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_id;

    if (has_media) {
        const UploadedFile file = save_file(options.media_path);
        if (!file.handle) throw std::runtime_error("edit_story: upload returned no handle");
        if (options.media_as_photo) {
            ty::inputMediaUploadedPhoto photo;
            photo.file = file.handle;
            req.media = std::make_shared<tl::InputMedia>(tl::InputMedia{photo});
        } else {
            ty::inputMediaUploadedDocument doc;
            doc.file = file.handle;
            doc.mime_type = video_mime_for(file.name);
            doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(
                tl::DocumentAttribute{ty::documentAttributeFilename{file.name}}));
            req.media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
        }
    }

    if (options.media_areas) {
        std::vector<std::shared_ptr<tl::MediaArea>> areas;
        for (const auto& area : *options.media_areas) {
            areas.push_back(std::make_shared<tl::MediaArea>(build_media_area(area)));
        }
        req.media_areas = std::move(areas);
    }

    if (options.caption) {
        req.caption = *options.caption;
        req.entities = std::vector<std::shared_ptr<tl::MessageEntity>>{};  // flags.1 covers caption and entities together
    }

    if (options.privacy_rules) {
        std::vector<std::shared_ptr<tl::InputPrivacyRule>> rules;
        for (const auto& rule : *options.privacy_rules) rules.push_back(build_privacy_rule(rule));
        req.privacy_rules = std::move(rules);
    }

    if (options.music) {
        const auto& m = *options.music;
        req.music = std::make_shared<tl::InputDocument>(
            tl::InputDocument{ty::inputDocument{m.id, m.access_hash, m.file_reference}});
    }

    (void)call_rpc(req);  // returns Updates; the edited story arrives through the update stream
    return true;
}

}  // namespace cppgram
