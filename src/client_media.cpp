#include "cppgram/client.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputChannel> channel_ref(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a channel or supergroup");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{p.id, p.access_hash}});
}

std::string mime_of(const std::string& name) {
    const std::string ext = std::filesystem::path(name).extension().string();
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".gif") return "image/gif";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".ogg" || ext == ".oga") return "audio/ogg";
    if (ext == ".opus") return "audio/opus";
    if (ext == ".wav") return "audio/wav";
    return "application/octet-stream";
}

MemberStatus status_of_chat_participant(const tl::ChatParticipant& p) {
    if (std::holds_alternative<ty::chatParticipantCreator>(p.value)) return MemberStatus::Creator;
    if (std::holds_alternative<ty::chatParticipantAdmin>(p.value)) return MemberStatus::Admin;
    return MemberStatus::Member;
}

std::int64_t user_id_of_chat_participant(const tl::ChatParticipant& p) {
    return std::visit([](const auto& v) { return static_cast<std::int64_t>(v.user_id); }, p.value);
}

}  // namespace

// ---- chat membership

ChatMember Client::get_chat_member(const PeerRef& chat, const PeerRef& user) {
    require_session();
    fn::channels_getParticipant req;
    req.channel = channel_ref(chat);
    req.participant = std::make_shared<tl::InputPeer>(input_peer(user));
    const tl::channels_ChannelParticipant result = session_->call(req);
    const auto* part = std::get_if<ty::channels_channelParticipant>(&result.value);
    if (part == nullptr) throw std::runtime_error("channels.getParticipant returned an unexpected type");
    cache_users_chats(part->users, part->chats);
    if (!part->participant) throw std::runtime_error("channels.getParticipant returned no participant");
    if (auto member = detail::chat_member_from_tl(*part->participant, cache_)) return *member;
    throw std::runtime_error("unsupported participant type");
}

std::vector<ChatMember> Client::get_chat_members(const PeerRef& chat, MemberFilter filter, std::int32_t limit) {
    require_session();
    if (chat.kind == PeerKind::Chat) {
        // Basic groups keep their members in messages.getFullChat, not in channels.getParticipants.
        fn::messages_getFullChat full_req;
        full_req.chat_id = chat.id;
        const tl::messages_ChatFull full = session_->call(full_req);
        std::vector<ChatMember> group_out;
        const auto* mcf = std::get_if<ty::messages_chatFull>(&full.value);
        if (mcf == nullptr || !mcf->full_chat) return group_out;
        cache_users_chats(mcf->users, mcf->chats);
        const auto* cf = std::get_if<ty::chatFull>(&mcf->full_chat->value);
        if (cf == nullptr || !cf->participants) return group_out;
        const auto* cps = std::get_if<ty::chatParticipants>(&cf->participants->value);
        if (cps == nullptr) return group_out;
        for (const auto& p : cps->participants) {
            if (!p || static_cast<std::int32_t>(group_out.size()) >= limit) continue;
            ChatMember m;
            m.user = cache_.find(PeerKind::User, std::visit([](const auto& v) { return static_cast<std::int64_t>(v.user_id); }, p->value))
                         .value_or(PeerRef{PeerKind::User, std::visit([](const auto& v) { return static_cast<std::int64_t>(v.user_id); }, p->value), 0, false});
            m.status = status_of_chat_participant(*p);
            group_out.push_back(m);
        }
        return group_out;
    }
    fn::channels_getParticipants req;
    req.channel = channel_ref(chat);
    if (filter == MemberFilter::Admins) {
        req.filter = std::make_shared<tl::ChannelParticipantsFilter>(tl::ChannelParticipantsFilter{ty::channelParticipantsAdmins{}});
    } else {
        req.filter = std::make_shared<tl::ChannelParticipantsFilter>(tl::ChannelParticipantsFilter{ty::channelParticipantsRecent{}});
    }
    req.offset = 0;
    req.limit = limit;
    req.hash = 0;
    const tl::channels_ChannelParticipants result = session_->call(req);
    std::vector<ChatMember> out;
    if (const auto* cp = std::get_if<ty::channels_channelParticipants>(&result.value)) {
        cache_users_chats(cp->users, cp->chats);
        for (const auto& p : cp->participants) {
            if (!p) continue;
            if (auto m = detail::chat_member_from_tl(*p, cache_)) out.push_back(*m);
        }
    }
    return out;
}

// ---- media senders

Message Client::send_animation(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.mime_type = mime_of(name);
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeAnimated{}}));
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{doc}), caption);
}

Message Client::send_audio(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.mime_type = mime_of(name);
    ty::documentAttributeAudio audio;
    audio.voice = false;
    audio.duration = 0;
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{audio}));
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{doc}), caption);
}

Message Client::send_voice(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.mime_type = mime_of(name) == "application/octet-stream" ? "audio/ogg" : mime_of(name);
    ty::documentAttributeAudio audio;
    audio.voice = true;
    audio.duration = 0;
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{audio}));
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{doc}), caption);
}

Message Client::send_video(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.mime_type = mime_of(name);
    ty::documentAttributeVideo video;
    video.supports_streaming = true;
    video.duration = 0;
    video.w = 0;
    video.h = 0;
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{video}));
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{doc}), caption);
}

Message Client::send_contact(const PeerRef& target, const std::string& phone, const std::string& first_name,
                             const std::string& last_name) {
    ty::inputMediaContact contact;
    contact.phone_number = phone;
    contact.first_name = first_name;
    contact.last_name = last_name;
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{contact}), "");
}

Message Client::send_location(const PeerRef& target, double latitude, double longitude) {
    ty::inputGeoPoint point;
    point.lat = latitude;
    point.long_ = longitude;
    ty::inputMediaGeoPoint media;
    media.geo_point = std::make_shared<tl::InputGeoPoint>(tl::InputGeoPoint{point});
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{media}), "");
}

Message Client::send_venue(const PeerRef& target, double latitude, double longitude, const std::string& title,
                           const std::string& address) {
    ty::inputGeoPoint point;
    point.lat = latitude;
    point.long_ = longitude;
    ty::inputMediaVenue venue;
    venue.geo_point = std::make_shared<tl::InputGeoPoint>(tl::InputGeoPoint{point});
    venue.title = title;
    venue.address = address;
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{venue}), "");
}

Message Client::send_dice(const PeerRef& target, const std::string& emoticon) {
    ty::inputMediaDice dice;
    dice.emoticon = emoticon;
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{dice}), "");
}

// ---- edits

Message Client::edit_message_caption(const PeerRef& target, std::int32_t message_id, const std::string& caption) {
    require_session();
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.message = caption;
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, caption, message_id)) return *msg;
    throw std::runtime_error("caption edited but the server returned no message");
}

Message Client::edit_message_media(const PeerRef& target, std::int32_t message_id, const std::string& path,
                                   const std::string& caption) {
    require_session();
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.mime_type = mime_of(name);
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.media = std::make_shared<tl::InputMedia>(tl::InputMedia{doc});
    if (!caption.empty()) req.message = caption;
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, caption, message_id)) return *msg;
    throw std::runtime_error("media edited but the server returned no message");
}

// ---- membership and deletion updates


void Client::on_chat_member_updated(ChatMemberHandler handler) { member_handlers_.push_back(std::move(handler)); }
void Client::on_join_request(JoinRequestHandler handler) { join_handlers_.push_back(std::move(handler)); }
void Client::on_deleted_messages(DeletedMessagesHandler handler) { deleted_handlers_.push_back(std::move(handler)); }

void Client::handle_membership_update(const tl::Update& update) {
    if (const auto* cp = std::get_if<ty::updateChannelParticipant>(&update.value)) {
        ChatMemberUpdate u;
        u.chat = cache_.find(PeerKind::Channel, cp->channel_id).value_or(PeerRef{PeerKind::Channel, cp->channel_id, 0, false});
        u.user = cache_.find(PeerKind::User, cp->user_id).value_or(PeerRef{PeerKind::User, cp->user_id, 0, false});
        u.actor_id = cp->actor_id;
        u.date = cp->date;
        if (cp->prev_participant && *cp->prev_participant) u.old_member = detail::chat_member_from_tl(**cp->prev_participant, cache_);
        if (cp->new_participant && *cp->new_participant) u.new_member = detail::chat_member_from_tl(**cp->new_participant, cache_);
        for (auto& h : member_handlers_) {
            try { h(*this, u); } catch (const std::exception& e) {
                std::cerr << "cppgram: member handler failed: " << e.what() << "\n";
            }
        }
        return;
    }
    if (const auto* cp = std::get_if<ty::updateChatParticipant>(&update.value)) {
        ChatMemberUpdate u;
        u.chat = PeerRef{PeerKind::Chat, cp->chat_id, 0, false};
        u.user = cache_.find(PeerKind::User, cp->user_id).value_or(PeerRef{PeerKind::User, cp->user_id, 0, false});
        u.actor_id = cp->actor_id;
        u.date = cp->date;
        if (cp->prev_participant && *cp->prev_participant) {
            ChatMember m;
            m.user = u.user;
            m.status = status_of_chat_participant(**cp->prev_participant);
            u.old_member = m;
        }
        if (cp->new_participant && *cp->new_participant) {
            ChatMember m;
            m.user = u.user;
            m.status = status_of_chat_participant(**cp->new_participant);
            u.new_member = m;
        } else if (cp->prev_participant && *cp->prev_participant) {
            // Removed from the group: no new participant record.
            ChatMember m;
            m.user = u.user;
            m.status = MemberStatus::Left;
            u.new_member = m;
        }
        (void)user_id_of_chat_participant;
        for (auto& h : member_handlers_) {
            try { h(*this, u); } catch (const std::exception& e) {
                std::cerr << "cppgram: member handler failed: " << e.what() << "\n";
            }
        }
        return;
    }
    if (const auto* jr = std::get_if<ty::updateBotChatInviteRequester>(&update.value)) {
        JoinRequest req;
        req.chat = jr->peer ? detail::peer_from_tl(*jr->peer, cache_) : PeerRef{};
        req.user_id = jr->user_id;
        req.about = jr->about;
        req.date = jr->date;
        for (auto& h : join_handlers_) {
            try { h(*this, req); } catch (const std::exception& e) {
                std::cerr << "cppgram: join request handler failed: " << e.what() << "\n";
            }
        }
        return;
    }
    if (const auto* dm = std::get_if<ty::updateDeleteMessages>(&update.value)) {
        DeletedMessages d;
        d.ids = dm->messages;
        for (auto& h : deleted_handlers_) {
            try { h(*this, d); } catch (const std::exception& e) {
                std::cerr << "cppgram: deleted messages handler failed: " << e.what() << "\n";
            }
        }
        return;
    }
    if (const auto* dc = std::get_if<ty::updateDeleteChannelMessages>(&update.value)) {
        DeletedMessages d;
        d.chat = cache_.find(PeerKind::Channel, dc->channel_id).value_or(PeerRef{PeerKind::Channel, dc->channel_id, 0, false});
        d.ids = dc->messages;
        for (auto& h : deleted_handlers_) {
            try { h(*this, d); } catch (const std::exception& e) {
                std::cerr << "cppgram: deleted messages handler failed: " << e.what() << "\n";
            }
        }
        return;
    }
}

}  // namespace cppgram
