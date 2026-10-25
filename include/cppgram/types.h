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
