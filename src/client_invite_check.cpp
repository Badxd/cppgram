#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

ChatInvitePreview Client::check_chat_invite(const std::string& invite_hash) {
    require_session();
    fn::messages_checkChatInvite req;
    req.hash = invite_hash;
    const tl::ChatInvite result = session_->call(req);

    ChatInvitePreview out;
    if (const auto* already = std::get_if<ty::chatInviteAlready>(&result.value)) {
        out.already_joined = true;
        if (already->chat) out.chat = detail::chat_from_tl(*already->chat);
        return out;
    }
    if (const auto* peek = std::get_if<ty::chatInvitePeek>(&result.value)) {
        out.peek = true;
        out.expires = peek->expires;
        if (peek->chat) out.chat = detail::chat_from_tl(*peek->chat);
        return out;
    }
    const auto* invite = std::get_if<ty::chatInvite>(&result.value);
    if (invite == nullptr) {
        throw std::runtime_error("check_chat_invite: unexpected invite type");
    }
    out.title = invite->title;
    out.about = invite->about;
    out.participants_count = invite->participants_count;
    out.is_channel = invite->channel;
    out.is_broadcast = invite->broadcast;
    out.is_megagroup = invite->megagroup;
    out.is_public = invite->public_;
    out.request_needed = invite->request_needed;
    out.verified = invite->verified;
    out.scam = invite->scam;
    out.fake = invite->fake;
    if (invite->participants) {
        for (const auto& user : *invite->participants) {
            if (!user) continue;
            if (auto parsed = detail::user_from_tl(*user)) out.participants.push_back(*parsed);
        }
    }
    return out;
}

}  // namespace cppgram
