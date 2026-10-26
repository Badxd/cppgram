#include "cppgram/client.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

std::vector<std::int32_t> Client::read_stories(const PeerRef& chat, std::int32_t max_id) {
    require_session();
    fn::stories_readStories req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.max_id = max_id;
    return session_->call(req);
}

bool Client::set_all_stories_hidden(bool hidden) {
    require_session();
    fn::stories_toggleAllStoriesHidden req;
    req.hidden = hidden;
    return session_->call(req);
}

bool Client::set_peer_stories_hidden(const PeerRef& chat, bool hidden) {
    require_session();
    fn::stories_togglePeerStoriesHidden req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.hidden = hidden;
    return session_->call(req);
}

void Client::send_story_reaction(const PeerRef& chat, std::int32_t story_id, const std::string& emoji,
                                 bool add_to_recent) {
    require_session();
    fn::stories_sendReaction req;
    req.add_to_recent = add_to_recent;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.story_id = story_id;
    req.reaction = std::make_shared<tl::Reaction>(tl::Reaction{ty::reactionEmoji{emoji}});
    session_->call(req);
}

}  // namespace cppgram
