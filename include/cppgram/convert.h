// cppgram - conversion from raw TL objects to high-level types, and the peer cache.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>

#include "cppgram/mtproto/schema/api.h"
#include "cppgram/parser.h"
#include "cppgram/types.h"

namespace cppgram::detail {

// Remembers access hashes and usernames seen in responses, so later requests can address peers.
class PeerCache {
public:
    void add_user(const mtproto::tl::User& user);
    void add_chat(const mtproto::tl::Chat& chat);
    void add_users(const std::vector<std::shared_ptr<mtproto::tl::User>>& users);
    void add_chats(const std::vector<std::shared_ptr<mtproto::tl::Chat>>& chats);

    void set_self(std::int64_t user_id) { self_id_ = user_id; }
    std::int64_t self_id() const noexcept { return self_id_; }

    // Returns the peer with its access hash when known.
    std::optional<PeerRef> find(PeerKind kind, std::int64_t id) const;
    std::optional<PeerRef> find_username(const std::string& username) const;

private:
    std::map<std::pair<int, std::int64_t>, PeerRef> peers_;
    std::map<std::string, PeerRef> usernames_;
    std::int64_t self_id_ = 0;
};

PeerRef peer_from_tl(const mtproto::tl::Peer& peer, const PeerCache& cache);
std::optional<ChatMember> chat_member_from_tl(const mtproto::tl::ChannelParticipant& participant,
                                               const PeerCache& cache);
std::optional<Message> message_from_tl(const mtproto::tl::Message& message, const PeerCache& cache);
// Message from a short update (updateShortMessage / updateShortChatMessage).
Message short_message(std::int32_t id, PeerRef chat, std::optional<PeerRef> sender, const std::string& text,
                      std::int32_t date, bool outgoing);
std::optional<User> user_from_tl(const mtproto::tl::User& user);
std::optional<Chat> chat_from_tl(const mtproto::tl::Chat& chat);
MediaInfo media_from_tl(const mtproto::tl::MessageMedia& media);
std::optional<PollInfo> poll_from_tl(const mtproto::tl::MessageMedia& media);
std::optional<Sticker> sticker_from_tl(const mtproto::tl::MessageMedia& media);
// Media info of a sticker or other document. Empty documents give nullopt.
std::optional<MediaInfo> media_info_from_document(const mtproto::tl::Document& doc);
StickerSetInfo sticker_set_info_from_tl(const mtproto::tl::types::stickerSet& set);
// Entity of a formatted text. Kinds this client does not model give nullopt.
std::shared_ptr<mtproto::tl::MessageEntity> to_tl_entity(const TextEntity& e);
std::optional<TextEntity> text_entity_from_tl(const mtproto::tl::MessageEntity& e);
// Saved star gift (payments.savedStarGifts entry). Empty entries give nullopt.
std::optional<SavedStarGiftInfo> saved_star_gift_from_tl(const mtproto::tl::SavedStarGift& wrapper);
// Regular star gift (payments.starGifts or resaleStarGifts entry). Other variants give nullopt.
std::optional<StarGiftInfo> star_gift_info_from_tl(const mtproto::tl::StarGift& wrapper);
// DialogInfo for each dialog, with the top message attached when it is among the decoded messages.
std::vector<DialogInfo> dialogs_from_tl(const std::vector<std::shared_ptr<mtproto::tl::Dialog>>& dialogs,
                                        const std::vector<Message>& messages, const PeerCache& cache);

}  // namespace cppgram::detail
