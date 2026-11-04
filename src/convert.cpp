#include "cppgram/convert.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <type_traits>
#include <variant>

namespace cppgram::detail {

namespace tl = mtproto::tl;
namespace ty = mtproto::tl::types;

namespace {

std::string lower_username(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int kind_key(PeerKind kind) {
    return static_cast<int>(kind);
}

}  // namespace

void PeerCache::add_user(const tl::User& user) {
    const auto* u = std::get_if<ty::user>(&user.value);
    if (u == nullptr) {
        return;
    }
    PeerRef ref;
    ref.kind = PeerKind::User;
    ref.id = u->id;
    ref.access_hash = u->access_hash.value_or(0);
    ref.has_access_hash = u->access_hash.has_value();
    peers_[{kind_key(PeerKind::User), ref.id}] = ref;
    if (u->username.has_value() && !u->username->empty()) {
        usernames_[lower_username(*u->username)] = ref;
    }
    if (u->self) {
        self_id_ = u->id;
    }
}

void PeerCache::add_chat(const tl::Chat& chat) {
    if (const auto* c = std::get_if<ty::channel>(&chat.value)) {
        PeerRef ref;
        ref.kind = PeerKind::Channel;
        ref.id = c->id;
        ref.access_hash = c->access_hash.value_or(0);
        ref.has_access_hash = c->access_hash.has_value();
        peers_[{kind_key(PeerKind::Channel), ref.id}] = ref;
        if (c->username.has_value() && !c->username->empty()) {
            usernames_[lower_username(*c->username)] = ref;
        }
    } else if (const auto* g = std::get_if<ty::chat>(&chat.value)) {
        PeerRef ref;
        ref.kind = PeerKind::Chat;
        ref.id = g->id;
        peers_[{kind_key(PeerKind::Chat), ref.id}] = ref;
    }
}

void PeerCache::add_users(const std::vector<std::shared_ptr<tl::User>>& users) {
    for (const auto& u : users) {
        if (u) add_user(*u);
    }
}

void PeerCache::add_chats(const std::vector<std::shared_ptr<tl::Chat>>& chats) {
    for (const auto& c : chats) {
        if (c) add_chat(*c);
    }
}

std::optional<PeerRef> PeerCache::find(PeerKind kind, std::int64_t id) const {
    const auto it = peers_.find({kind_key(kind), id});
    if (it == peers_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<PeerRef> PeerCache::find_username(const std::string& username) const {
    const auto it = usernames_.find(lower_username(username));
    if (it == usernames_.end()) {
        return std::nullopt;
    }
    return it->second;
}

PeerRef peer_from_tl(const tl::Peer& peer, const PeerCache& cache) {
    PeerRef ref;
    if (const auto* u = std::get_if<ty::peerUser>(&peer.value)) {
        ref.kind = PeerKind::User;
        ref.id = u->user_id;
    } else if (const auto* c = std::get_if<ty::peerChat>(&peer.value)) {
        ref.kind = PeerKind::Chat;
        ref.id = c->chat_id;
    } else if (const auto* ch = std::get_if<ty::peerChannel>(&peer.value)) {
        ref.kind = PeerKind::Channel;
        ref.id = ch->channel_id;
    }
    if (const auto known = cache.find(ref.kind, ref.id)) {
        ref.access_hash = known->access_hash;
        ref.has_access_hash = known->has_access_hash;
    }
    return ref;
}

std::optional<Sticker> sticker_from_tl(const tl::MessageMedia& media) {
    const auto* dm = std::get_if<ty::messageMediaDocument>(&media.value);
    if (dm == nullptr || !dm->document || !*dm->document) return std::nullopt;
    const auto* d = std::get_if<ty::document>(&(*dm->document)->value);
    if (d == nullptr) return std::nullopt;
    Sticker st;
    bool found = false;
    for (const auto& attr : d->attributes) {
        if (!attr) continue;
        if (const auto* sa = std::get_if<ty::documentAttributeSticker>(&attr->value)) {
            found = true;
            st.emoji = sa->alt;
            if (sa->stickerset) {
                if (const auto* id = std::get_if<ty::inputStickerSetID>(&sa->stickerset->value)) {
                    st.set_id = id->id;
                    st.set_access_hash = id->access_hash;
                } else if (const auto* sn = std::get_if<ty::inputStickerSetShortName>(&sa->stickerset->value)) {
                    st.set_short_name = sn->short_name;
                }
            }
        }
    }
    if (!found) return std::nullopt;
    st.media = media_from_tl(media);
    return st;
}

std::optional<PollInfo> poll_from_tl(const tl::MessageMedia& media) {
    const auto* pm = std::get_if<ty::messageMediaPoll>(&media.value);
    if (pm == nullptr || !pm->poll) return std::nullopt;
    const auto* pd = std::get_if<ty::poll>(&pm->poll->value);
    if (pd == nullptr) return std::nullopt;

    PollInfo info;
    info.id = pd->id;
    if (pd->question) {
        if (const auto* q = std::get_if<ty::textWithEntities>(&pd->question->value)) info.question = q->text;
    }
    info.closed = pd->closed;
    info.multiple_choice = pd->multiple_choice;
    info.quiz = pd->quiz;
    info.public_voters = pd->public_voters;

    const std::vector<std::shared_ptr<tl::PollAnswerVoters>>* voters = nullptr;
    if (pm->results) {
        if (const auto* r = std::get_if<ty::pollResults>(&pm->results->value)) {
            if (r->results) voters = &*r->results;
        }
    }
    for (const auto& answer : pd->answers) {
        if (!answer) continue;
        const auto* a = std::get_if<ty::pollAnswer>(&answer->value);
        if (a == nullptr) continue;
        PollOption opt;
        if (a->text) {
            if (const auto* t = std::get_if<ty::textWithEntities>(&a->text->value)) opt.text = t->text;
        }
        opt.option_id.assign(a->option.begin(), a->option.end());
        if (voters) {
            for (const auto& v : *voters) {
                if (!v) continue;
                const auto* vv = std::get_if<ty::pollAnswerVoters>(&v->value);
                if (vv == nullptr || vv->option != a->option) continue;
                opt.voters = vv->voters.value_or(0);
                opt.chosen = vv->chosen;
                opt.correct = vv->correct;
            }
        }
        info.options.push_back(std::move(opt));
    }
    return info;
}

MediaInfo media_from_tl(const tl::MessageMedia& media) {
    MediaInfo info;
    if (const auto* poll_media = std::get_if<ty::messageMediaPoll>(&media.value)) {
        if (poll_media->poll && std::holds_alternative<ty::poll>(poll_media->poll->value)) {
            info.kind = MediaKind::Poll;
            info.id = std::get<ty::poll>(poll_media->poll->value).id;
        }
        return info;
    }
    if (const auto* photo_media = std::get_if<ty::messageMediaPhoto>(&media.value)) {
        if (photo_media->photo && *photo_media->photo) {
            if (const auto* p = std::get_if<ty::photo>(&(*photo_media->photo)->value)) {
                info.kind = MediaKind::Photo;
                info.id = p->id;
                info.access_hash = p->access_hash;
                info.file_reference = p->file_reference;
                info.dc_id = p->dc_id;
            }
        }
        return info;
    }
    if (const auto* doc_media = std::get_if<ty::messageMediaDocument>(&media.value)) {
        if (doc_media->document && *doc_media->document) {
            if (const auto* d = std::get_if<ty::document>(&(*doc_media->document)->value)) {
                info.kind = MediaKind::Document;
                info.id = d->id;
                info.access_hash = d->access_hash;
                info.file_reference = d->file_reference;
                info.mime_type = d->mime_type;
                info.size = d->size;
                info.dc_id = d->dc_id;
                for (const auto& attr : d->attributes) {
                    if (!attr) continue;
                    if (const auto* f = std::get_if<ty::documentAttributeFilename>(&attr->value)) {
                        info.file_name = f->file_name;
                    } else if (std::get_if<ty::documentAttributeVideo>(&attr->value)) {
                        info.is_video = true;
                    } else if (const auto* a = std::get_if<ty::documentAttributeAudio>(&attr->value)) {
                        info.is_voice = a->voice;
                    } else if (std::get_if<ty::documentAttributeSticker>(&attr->value)) {
                        info.is_sticker = true;
                    }
                }
            }
        }
        return info;
    }
    info.kind = MediaKind::Other;
    return info;
}

std::optional<Message> message_from_tl(const tl::Message& message, const PeerCache& cache) {
    if (const auto* m = std::get_if<ty::message>(&message.value)) {
        Message out;
        out.id = m->id;
        out.chat = peer_from_tl(*m->peer_id, cache);
        if (m->from_id && *m->from_id) {
            out.sender = peer_from_tl(**m->from_id, cache);
        }
        out.text = m->message;
        out.date = m->date;
        out.outgoing = m->out;
        if (m->reply_to && *m->reply_to) {
            if (const auto* rh = std::get_if<ty::messageReplyHeader>(&(*m->reply_to)->value)) {
                out.reply_to_id = rh->reply_to_msg_id;
            }
        }
        if (m->media && *m->media) {
            out.media = media_from_tl(**m->media);
            out.poll = poll_from_tl(**m->media);
            out.sticker = sticker_from_tl(**m->media);
        }
        return out;
    }
    if (const auto* s = std::get_if<ty::messageService>(&message.value)) {
        Message out;
        out.id = s->id;
        out.chat = peer_from_tl(*s->peer_id, cache);
        if (s->from_id && *s->from_id) {
            out.sender = peer_from_tl(**s->from_id, cache);
        }
        out.date = s->date;
        out.outgoing = s->out;
        out.is_service = true;
        return out;
    }
    return std::nullopt;
}

Message short_message(std::int32_t id, PeerRef chat, std::optional<PeerRef> sender, const std::string& text,
                      std::int32_t date, bool outgoing) {
    Message out;
    out.id = id;
    out.chat = chat;
    out.sender = std::move(sender);
    out.text = text;
    out.date = date;
    out.outgoing = outgoing;
    return out;
}

std::optional<User> user_from_tl(const tl::User& user) {
    const auto* u = std::get_if<ty::user>(&user.value);
    if (u == nullptr) {
        return std::nullopt;
    }
    User out;
    out.id = u->id;
    out.access_hash = u->access_hash.value_or(0);
    out.first_name = u->first_name.value_or("");
    out.last_name = u->last_name.value_or("");
    out.username = u->username.value_or("");
    out.phone = u->phone.value_or("");
    out.is_bot = u->bot;
    out.is_self = u->self;
    out.is_premium = u->premium;
    return out;
}

std::optional<Chat> chat_from_tl(const tl::Chat& chat) {
    if (const auto* c = std::get_if<ty::channel>(&chat.value)) {
        Chat out;
        out.id = c->id;
        out.access_hash = c->access_hash.value_or(0);
        out.title = c->title;
        out.username = c->username.value_or("");
        out.kind = PeerKind::Channel;
        out.type = c->broadcast ? ChatType::Channel : ChatType::Supergroup;
        return out;
    }
    if (const auto* g = std::get_if<ty::chat>(&chat.value)) {
        Chat out;
        out.id = g->id;
        out.title = g->title;
        out.kind = PeerKind::Chat;
        out.type = ChatType::Group;
        return out;
    }
    return std::nullopt;
}

std::optional<ChatMember> chat_member_from_tl(const tl::ChannelParticipant& participant, const PeerCache& cache) {
    ChatMember m;
    const auto user_ref = [&](std::int64_t id) {
        return cache.find(PeerKind::User, id).value_or(PeerRef{PeerKind::User, id, 0, false});
    };
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ty::channelParticipantCreator>) {
                m.user = user_ref(v.user_id);
                m.status = MemberStatus::Creator;
                m.rank = v.rank.value_or("");
            } else if constexpr (std::is_same_v<T, ty::channelParticipantAdmin>) {
                m.user = user_ref(v.user_id);
                m.status = MemberStatus::Admin;
                m.rank = v.rank.value_or("");
                m.can_edit = v.can_edit;
            } else if constexpr (std::is_same_v<T, ty::channelParticipant> || std::is_same_v<T, ty::channelParticipantSelf>) {
                m.user = user_ref(v.user_id);
                m.status = MemberStatus::Member;
                m.rank = v.rank.value_or("");
            } else if constexpr (std::is_same_v<T, ty::channelParticipantBanned>) {
                m.user = v.peer ? peer_from_tl(*v.peer, cache) : PeerRef{};
                m.status = v.left ? MemberStatus::Left : MemberStatus::Banned;
                m.rank = v.rank.value_or("");
            } else if constexpr (std::is_same_v<T, ty::channelParticipantLeft>) {
                m.user = v.peer ? peer_from_tl(*v.peer, cache) : PeerRef{};
                m.status = MemberStatus::Left;
            }
        },
        participant.value);
    return m;
}

}  // namespace cppgram::detail
