// cppgram - high-level types built from the raw TL schema.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <string>

#include "cppgram/mtproto/tl.h"

namespace cppgram {

enum class PeerKind { User, Chat, Channel };

// Identifies a user, basic group or channel. access_hash is needed for users and channels.
struct PeerRef {
    PeerKind kind = PeerKind::User;
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    bool has_access_hash = false;
};

enum class ChatType { Private, Group, Supergroup, Channel };

struct User {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    std::string first_name;
    std::string last_name;
    std::string username;
    std::string phone;
    bool is_bot = false;
    bool is_self = false;
    bool is_premium = false;
};

struct Chat {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    std::string title;
    std::string username;
    std::string about;          // from the full info (get_chat)
    std::int32_t member_count = 0;  // from the full info when Telegram reports it
    ChatType type = ChatType::Group;
    PeerKind kind = PeerKind::Chat;
};

// A file to turn into a sticker. path is a .webp, .png or .tgs file; emoji is the sticker's emoji.
struct StickerFile {
    std::string path;
    std::string emoji;
};

enum class MediaKind { None, Photo, Document, Other, Poll };

// Location data needed to download or forward a media file.
struct MediaInfo {
    MediaKind kind = MediaKind::None;
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    mtproto::Bytes file_reference;
    std::string mime_type;
    std::string file_name;
    std::int64_t size = 0;
    std::int32_t dc_id = 0;
    bool is_video = false;
    bool is_voice = false;
    bool is_sticker = false;
};

// One answer of a poll. option_id is the raw bytes Telegram uses to identify it when voting.
struct PollOption {
    std::string text;
    std::string option_id;
    std::int32_t voters = 0;
    bool chosen = false;   // true if the current account voted for it
    bool correct = false;  // quiz polls: the right answer, once the poll is closed or results are visible
};

struct PollInfo {
    std::int64_t id = 0;
    std::string question;
    std::vector<PollOption> options;
    bool closed = false;
    bool multiple_choice = false;
    bool quiz = false;
    bool public_voters = false;
};

// A sticker document with its emoji and set reference (from the document's sticker attribute).
struct Sticker {
    MediaInfo media;
    std::string emoji;
    std::int64_t set_id = 0;
    std::int64_t set_access_hash = 0;
    std::string set_short_name;  // only when Telegram sent the set by short name
};

struct Message {
    std::int32_t id = 0;
    PeerRef chat;
    std::optional<PeerRef> sender;
    std::string text;
    std::int32_t date = 0;
    bool outgoing = false;
    std::optional<std::int32_t> reply_to_id;
    MediaInfo media;
    std::optional<PollInfo> poll;  // set when media.kind == MediaKind::Poll
    std::optional<Sticker> sticker;  // set when the media is a sticker
    bool is_service = false;
};

struct CallbackQuery {
    std::int64_t id = 0;
    std::int64_t user_id = 0;
    PeerRef chat;
    std::int32_t message_id = 0;
    std::string data;
};

// Chat action sent with messages.setTyping.
enum class ChatAction { Typing, UploadPhoto, UploadVideo, UploadDocument, RecordVoice, RecordVideo, Cancel };

}  // namespace cppgram

namespace cppgram {

// Admin rights for promote_chat_member. Set a field to true to grant it.
struct AdminRights {
    bool change_info = false;
    bool delete_messages = false;
    bool ban_users = false;
    bool invite_users = false;
    bool pin_messages = false;
    bool manage_call = false;
    bool manage_topics = false;
    bool add_admins = false;
    bool anonymous = false;
};

// Restrictions for restrict_chat_member. Defaults allow everything; set false to take it away.
struct Restrictions {
    bool can_send_messages = true;
    bool can_send_media = true;
    bool can_send_stickers = true;
    bool can_send_polls = true;
    bool can_embed_links = true;
    bool can_change_info = true;
    bool can_invite_users = true;
    bool can_pin_messages = true;
};

struct ContactInput {
    std::string phone;
    std::string first_name;
    std::string last_name;
};

struct BotCommandSpec {
    std::string command;
    std::string description;
};

// An article-style inline result (text only). Media results are not wrapped yet.
struct InlineArticle {
    std::string id;
    std::string title;
    std::string description;
    std::string text;
};

struct InlineQuery {
    std::int64_t id = 0;
    std::int64_t user_id = 0;
    std::string query;
    std::string offset;
};

}  // namespace cppgram

namespace cppgram {

enum class MemberStatus { Creator, Admin, Member, Restricted, Left, Banned };

struct ChatMember {
    PeerRef user;
    MemberStatus status = MemberStatus::Member;
    std::string rank;
    bool can_edit = false;  // admins only: whether the caller may change this admin
};

struct ChatMemberUpdate {
    PeerRef chat;
    PeerRef user;
    std::int64_t actor_id = 0;
    std::optional<ChatMember> old_member;
    std::optional<ChatMember> new_member;
    std::int32_t date = 0;
};

struct JoinRequest {
    PeerRef chat;
    std::int64_t user_id = 0;
    std::string about;
    std::int32_t date = 0;
};

struct DeletedMessages {
    std::optional<PeerRef> chat;  // empty for basic-group/private deletes that carry no peer
    std::vector<std::int32_t> ids;
};

enum class MemberFilter { Recent, Admins };

}  // namespace cppgram

namespace cppgram {

// Inline (under-message) keyboard button. Build with the static helpers.
struct InlineButton {
    enum class Kind { Callback, Url, SwitchInline, Copy };
    Kind kind = Kind::Callback;
    std::string text;
    std::string value;        // callback data, url, switch-inline query, or text to copy
    bool same_peer = false;   // switch-inline only: query the current chat

    static InlineButton callback(std::string text, std::string data) {
        return {Kind::Callback, std::move(text), std::move(data), false};
    }
    static InlineButton url(std::string text, std::string url) {
        return {Kind::Url, std::move(text), std::move(url), false};
    }
    static InlineButton switch_inline(std::string text, std::string query, bool current_chat = false) {
        return {Kind::SwitchInline, std::move(text), std::move(query), current_chat};
    }
    static InlineButton copy_text(std::string text, std::string to_copy) {
        return {Kind::Copy, std::move(text), std::move(to_copy), false};
    }
};

using InlineKeyboard = std::vector<std::vector<InlineButton>>;

// Reply keyboard (replaces the user's keyboard). Each inner vector is one row of button labels.
struct ReplyKeyboard {
    std::vector<std::vector<std::string>> rows;
    bool resize = true;
    bool one_time = false;
    bool selective = false;
    bool persistent = false;
    std::string placeholder;
};

}  // namespace cppgram

namespace cppgram {

struct StickerSetInfo {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    std::string title;
    std::string short_name;
    std::int32_t count = 0;
};

struct StickerSetContent {
    StickerSetInfo info;
    std::vector<MediaInfo> stickers;
};

}  // namespace cppgram

#include <variant>

namespace cppgram {

struct InlineCachedPhoto {
    std::string id;
    MediaInfo photo;  // a photo already on Telegram (Message::media of a photo message)
    std::string caption;
};

struct InlineCachedDocument {
    std::string id;
    std::string title;
    std::string description;
    MediaInfo document;  // a document already on Telegram
    std::string caption;
};

struct InlineGame {
    std::string id;
    std::string short_name;  // the game's short name registered with BotFather
};

using InlineResultSpec = std::variant<InlineArticle, InlineCachedPhoto, InlineCachedDocument, InlineGame>;

struct GameScore {
    PeerRef user;
    std::int32_t position = 0;
    std::int32_t score = 0;
};

}  // namespace cppgram

namespace cppgram {

struct ForumTopicInfo {
    std::int32_t id = 0;  // the topic id (the id of its service message)
    std::string title;
    std::int32_t date = 0;
    std::int32_t icon_color = 0;
    bool closed = false;
    bool pinned = false;
    bool hidden = false;
};

enum class PrivacyKey { LastSeen, ProfilePhoto, PhoneNumber, Forwards, Invite };
enum class PrivacyLevel { Everyone, Contacts, Nobody };

// A document (sticker, file) that is already stored on Telegram. Enough to reference it again.
struct DocumentRef {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    mtproto::Bytes file_reference;
};

struct PasswordInfo {
    bool has_password = false;  // a two-step verification password is set
    std::string hint;
};

struct BotInfoText {
    std::string name;
    std::string about;
    std::string description;
};

struct DialogInfo {
    PeerRef peer;
    std::int32_t top_message = 0;
    std::int32_t unread_count = 0;
    bool pinned = false;
    std::optional<Message> top;  // the top message, when Telegram sent it along
};

}  // namespace cppgram

namespace cppgram {

// A chat folder (dialog filter). Folder ids 2..255 are user folders; 0 and 1 are reserved.
struct FolderInfo {
    std::int32_t id = 0;
    std::string title;
    std::vector<PeerRef> include_peers;
    std::vector<PeerRef> exclude_peers;
    bool contacts = false;
    bool non_contacts = false;
    bool groups = false;
    bool broadcasts = false;
    bool bots = false;
    bool exclude_muted = false;
    bool exclude_read = false;
    bool exclude_archived = false;
};

// An exported invite link with its admin, usage and state (phase 2 invite API).
struct InviteLinkInfo {
    std::string link;
    std::string title;
    std::int64_t admin_id = 0;
    std::int32_t date = 0;
    std::optional<std::int32_t> start_date;
    std::optional<std::int32_t> expire_date;
    std::optional<std::int32_t> usage_limit;
    std::int32_t usage = 0;
    std::int32_t requested = 0;
    bool revoked = false;
    bool permanent = false;
    bool request_needed = false;
};

// Per-admin invite counts for a chat.
struct AdminInviteCount {
    std::int64_t admin_id = 0;
    std::int32_t invites = 0;
    std::int32_t revoked_invites = 0;
};

// Phase 3/4 result types.
struct MessageReadParticipant {
    std::int64_t user_id = 0;
    std::int32_t date = 0;
};

struct AvailableEffectInfo {
    std::int64_t id = 0;
    std::string emoticon;
    bool premium_required = false;
};

struct StoryInfo {
    std::int32_t id = 0;
    std::int32_t date = 0;
    std::int32_t expire_date = 0;
    bool pinned = false;
    bool noforwards = false;
    std::string caption;
};

// Phase 5/6 result types.
struct FolderInviteInfo {
    std::string url;
    std::string title;
    std::int32_t peer_count = 0;
};

struct FolderInviteCheck {
    bool already_joined = false;
    std::string title;
    std::int32_t peer_count = 0;
};

struct BoostsStatusInfo {
    bool my_boost = false;
    std::int32_t level = 0;
    std::int32_t current_level_boosts = 0;
    std::int32_t boosts = 0;
    std::int32_t gift_boosts = 0;
    std::optional<std::int32_t> next_level_boosts;
    std::string boost_url;
};

// Phase 7/8 result types.
// One checklist task. id must be unique within its checklist.
struct TodoItemInput {
    std::int32_t id = 0;
    std::string text;
};

// Receipt of a paid invoice (phase 26).
struct PaymentReceiptInfo {
    std::int32_t date = 0;
    std::int64_t bot_id = 0;
    std::string title;
    std::string description;
};

// A bot connected to the account for business messages (phase 22).
struct ConnectedBotInfo {
    std::int64_t bot_id = 0;
    std::int32_t date = 0;
    std::string device;
    std::string location;
};

// A gift the account shows, hides, or keeps on a peer's profile (phase 18).
struct SavedStarGiftInfo {
    std::optional<std::int32_t> msg_id;    // service message id, for gifts in the account's own chat
    std::optional<std::int64_t> saved_id;  // id for gifts saved to a channel
    std::int64_t gift_id = 0;              // regular gift id; 0 for unique gifts
    std::string unique_slug;               // slug of a unique gift; empty for regular gifts
    std::int32_t date = 0;
    bool unsaved = false;
    bool name_hidden = false;
    bool pinned_to_top = false;
    bool can_upgrade = false;
    std::optional<std::int64_t> convert_stars;
    std::optional<std::int64_t> upgrade_stars;
    std::optional<std::int64_t> transfer_stars;
    std::optional<std::string> message;    // text the sender attached
};

// One page of saved gifts. next_offset is empty when there are no more pages.
struct SavedStarGiftPage {
    std::int32_t count = 0;
    std::vector<SavedStarGiftInfo> gifts;
    std::string next_offset;
};

struct StarGiftCollectionInfo {
    std::int32_t id = 0;
    std::string title;
    std::int32_t gifts_count = 0;
};

// A unique (upgraded) gift looked up by its slug.
struct UniqueStarGiftInfo {
    std::int64_t id = 0;
    std::int64_t gift_id = 0;  // the regular gift it was upgraded from
    std::string title;
    std::string slug;
    std::int32_t num = 0;
};

struct StarGiftInfo {
    std::int64_t id = 0;
    std::int64_t stars = 0;
    bool limited = false;
    bool sold_out = false;
    std::optional<std::int32_t> remaining;
    std::optional<std::int32_t> total;
};

struct StarsTransactionInfo {
    std::string id;
    std::int64_t amount = 0;  // whole Stars, negative for spending
    std::int32_t date = 0;
    bool refund = false;
    bool pending = false;
    bool failed = false;
};

// Phase 9/10 result types.
struct GlobalPrivacyInfo {
    bool archive_and_mute_new_noncontact_peers = false;
    bool keep_archived_unmuted = false;
    bool keep_archived_folders = false;
    bool new_noncontact_peers_require_premium = false;
    bool display_gifts_button = false;
};

// Phase 11/12 result types.
struct QuickReplyInfo {
    std::int32_t shortcut_id = 0;
    std::string shortcut;
    std::int32_t message_count = 0;
};

struct BlockedUserInfo {
    std::int64_t user_id = 0;
    std::int32_t date = 0;
};

// Result of previewing an invite hash without joining (messages.checkChatInvite).
struct ChatInvitePreview {
    bool already_joined = false;  // the account is already a member; chat is set
    bool peek = false;            // preview of a chat the account has not joined
    std::optional<Chat> chat;     // set for already_joined and peek
    std::int32_t expires = 0;     // peek only: unix time the preview expires
    std::string title;
    std::optional<std::string> about;
    std::int32_t participants_count = 0;
    std::vector<User> participants;  // a few members the server chooses to show
    bool is_channel = false;
    bool is_broadcast = false;
    bool is_megagroup = false;
    bool is_public = false;
    bool request_needed = false;  // joining needs admin approval
    bool verified = false;
    bool scam = false;
    bool fake = false;
};

struct ChatInviteJoiner {
    PeerRef user;
    std::int32_t date = 0;
    std::string about;
    bool requested = false;  // waiting for admin approval
};

// A profile photo of a user (or of the logged-in account).
struct ProfilePhoto {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    mtproto::Bytes file_reference;
    std::int32_t date = 0;
    std::int32_t dc_id = 0;
    bool has_video = false;
};

// One logged-in session of the account (account.getAuthorizations).
struct SessionInfo {
    std::int64_t hash = 0;
    std::string device_model;
    std::string platform;
    std::string system_version;
    std::string app_name;
    std::string app_version;
    std::int32_t api_id = 0;
    std::int32_t date_created = 0;
    std::int32_t date_active = 0;
    std::string ip;
    std::string country;
    std::string region;
    bool current = false;
    bool official_app = false;
    bool password_pending = false;
    bool unconfirmed = false;
};

}  // namespace cppgram
