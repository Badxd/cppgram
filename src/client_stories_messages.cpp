#include "cppgram/client.h"

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
    const auto res = session_->call(req);
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
    session_->call(req);
}

std::vector<AvailableEffectInfo> Client::get_available_effects() {
    require_session();
    fn::messages_getAvailableEffects req;
    req.hash = 0;
    const tl::messages_AvailableEffects res = session_->call(req);
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
    session_->call(req);
}

void Client::clear_reaction(const PeerRef& chat, std::int32_t message_id) {
    require_session();
    fn::messages_sendReaction req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.msg_id = message_id;
    req.reaction = std::vector<std::shared_ptr<tl::Reaction>>{};
    session_->call(req);
}

// ---- phase 4: stories

std::vector<StoryInfo> Client::get_pinned_stories(const PeerRef& chat, std::int32_t offset_id, std::int32_t limit) {
    require_session();
    fn::stories_getPinnedStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset_id = offset_id;
    req.limit = limit;
    return to_story_list(session_->call(req));
}

std::vector<StoryInfo> Client::get_stories_archive(const PeerRef& chat, std::int32_t offset_id, std::int32_t limit) {
    require_session();
    fn::stories_getStoriesArchive req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.offset_id = offset_id;
    req.limit = limit;
    return to_story_list(session_->call(req));
}

std::vector<StoryInfo> Client::get_stories_by_id(const PeerRef& chat, const std::vector<std::int32_t>& story_ids) {
    require_session();
    fn::stories_getStoriesByID req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    return to_story_list(session_->call(req));
}

std::vector<std::int32_t> Client::delete_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids) {
    require_session();
    fn::stories_deleteStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    return session_->call(req);
}

std::vector<std::int32_t> Client::toggle_pinned_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids,
                                                        bool pinned) {
    require_session();
    fn::stories_togglePinned req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_ids;
    req.pinned = pinned;
    return session_->call(req);
}

std::string Client::export_story_link(const PeerRef& chat, std::int32_t story_id) {
    require_session();
    fn::stories_exportStoryLink req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.id = story_id;
    const tl::ExportedStoryLink res = session_->call(req);
    const auto* link = std::get_if<ty::exportedStoryLink>(&res.value);
    if (link == nullptr) throw std::runtime_error("export_story_link: no link returned");
    return link->link;
}

}  // namespace cppgram
