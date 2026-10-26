// cppgram - high-level client: connection, updates dispatcher, filters, messages, chats, media.
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cppgram/convert.h"
#include "cppgram/filters.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/parser.h"
#include "cppgram/types.h"

namespace cppgram {

struct ClientOptions {
    std::int32_t api_id = 0;
    std::string api_hash;
    // Telegram production DC addresses: DC1 149.154.175.53, DC2 149.154.167.51, DC3 149.154.175.100,
    // DC4 149.154.167.91, DC5 91.108.56.130. Port 443 is used.
    std::string dc_ip = "149.154.167.51";
    std::uint16_t dc_port = 443;
    std::int32_t dc_id = 2;
    std::string session_path = "cppgram.session";
    // Base64 session from export_session_string(). Used only when no saved session exists yet.
    std::string session_string;
    std::string device_model = "cppgram";
    std::string system_version = "linux";
    std::string app_version = "0.1.0";
    std::string lang_code = "en";
    // Idle time before run() sends a keepalive ping.
    int keepalive_ms = 15000;
    // Socket receive timeout used while connecting (TCP + handshake + initConnection).
    // Expiry throws mtproto::ConnectionTimeout instead of blocking forever.
    int connect_timeout_ms = 10000;
    // Checks each CDN download part against the SHA-256 Telegram sends with it, before writing it to disk.
    bool verify_cdn_hashes = true;
    // Prints each connection stage (TCP, each handshake packet, session setup) to stderr.
    bool debug = false;
};

class SqliteStorage;

// A file uploaded with Client::save_file. handle is the InputFile reference Telegram returned.
struct UploadedFile {
    std::string name;
    std::int64_t size = 0;
    std::shared_ptr<mtproto::tl::InputFile> handle;
};

class Client {
public:
    using MessageHandler = std::function<void(Client&, const Message&)>;
    using CallbackHandler = std::function<void(Client&, const CallbackQuery&)>;
    using RawUpdateHandler = std::function<void(Client&, const mtproto::tl::Update&)>;
    using InlineQueryHandler = std::function<void(Client&, const InlineQuery&)>;
    using ChatMemberHandler = std::function<void(Client&, const ChatMemberUpdate&)>;
    using JoinRequestHandler = std::function<void(Client&, const JoinRequest&)>;
    using DeletedMessagesHandler = std::function<void(Client&, const DeletedMessages&)>;
    using DownloadProgress = std::function<void(std::int64_t done, std::int64_t total)>;
    // stage is the handler kind that threw (for example "message handler"), message is the exception text.
    using ErrorHandler = std::function<void(Client&, const std::string& stage, const std::string& message)>;

    explicit Client(ClientOptions options);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Opens the connection, creates or loads the auth key, and runs initConnection.
    void connect();
    void disconnect();
    bool is_connected() const noexcept { return session_ != nullptr; }

    // Feeds one raw TL update body (updates, updateShortMessage, ...) through the dispatcher.
    // Used by the update loop and useful for custom transports or tests.
    void dispatch_raw_update(const mtproto::Bytes& body) { handle_update_body(body); }

    // ---- update state (pts/qts/date/seq) and gap recovery
    // Fetches updates.getState and starts tracking pts. login_bot() calls this.
    void sync_updates_state();
    // Runs updates.getDifference until the server reports the state is current.
    void catch_up();

    void login_bot(const std::string& bot_token);
    User get_me();

    // ---- handlers and update loop
    // Handlers in a lower group run first. Groups are ordered; handlers inside one group keep insertion order.
    void on_message(Filter filter, MessageHandler handler, int group = 0);
    // Called whenever a handler throws. Without one, the error is printed to stderr.
    void on_error(ErrorHandler handler);
    void on_callback_query(CallbackHandler handler);
    // Filtered overloads: the handler runs only when the filter matches.
    void on_callback_query(CallbackFilter filter, CallbackHandler handler) {
        on_callback_query([filter = std::move(filter), handler = std::move(handler)](Client& c, const CallbackQuery& q) {
            if (filter(q)) handler(c, q);
        });
    }
    void on_inline_query(InlineFilter filter, InlineQueryHandler handler) {
        on_inline_query([filter = std::move(filter), handler = std::move(handler)](Client& c, const InlineQuery& q) {
            if (filter(q)) handler(c, q);
        });
    }
    // Called for every update before the typed handlers. Gets the raw TL update variant.
    void on_raw_update(RawUpdateHandler handler);
    // Blocks, dispatching updates to handlers, until stop() is called from a handler or another thread.
    void run();
    void stop() noexcept { running_ = false; }

    // ---- raw access: any generated request
    template <class Request>
    typename Request::Result invoke(const Request& request) {
        require_session();
        return session_->call(request);
    }

    // ---- peers
    PeerRef resolve(const std::string& username_or_me);
    mtproto::tl::InputPeer input_peer(const PeerRef& peer) const;

    // ---- messages
    Message send_message(const std::string& target, const std::string& text,
                         std::optional<std::int32_t> reply_to_id = std::nullopt);
    // effect_id: Telegram message effect (for example a confetti or fire animation). Pass std::nullopt for none.
    Message send_message(const PeerRef& target, const std::string& text,
                         std::optional<std::int32_t> reply_to_id = std::nullopt,
                         std::optional<std::int64_t> effect_id = std::nullopt);
    Message edit_message(const PeerRef& target, std::int32_t message_id, const std::string& text);
    void delete_messages(const PeerRef& target, const std::vector<std::int32_t>& message_ids, bool revoke = true);
    std::vector<Message> get_messages(const PeerRef& target, const std::vector<std::int32_t>& message_ids);
    std::vector<Message> get_history(const PeerRef& target, std::int32_t limit = 100, std::int32_t offset_id = 0);
    void send_chat_action(const PeerRef& target, ChatAction action);
    void pin_message(const PeerRef& target, std::int32_t message_id, bool silent = false);
    void unpin_message(const PeerRef& target, std::int32_t message_id);
    std::vector<Message> forward_messages(const PeerRef& from, const PeerRef& to,
                                          const std::vector<std::int32_t>& message_ids);
    void answer_callback_query(std::int64_t query_id, const std::string& text = "", bool alert = false,
                               const std::string& url = "");

    // ---- media
    Message send_document(const PeerRef& target, const std::string& path, const std::string& caption = "");
    Message send_photo(const PeerRef& target, const std::string& path, const std::string& caption = "");
    // Downloads the media of a message to `path`. Returns the number of bytes written.
    std::int64_t download_media(const Message& message, const std::string& path);
    // Same as above, and calls progress(bytes_done, total_bytes) after each chunk. total is 0 when Telegram did not report a size.
    std::int64_t download_media(const Message& message, const std::string& path, DownloadProgress progress);

    // ---- handlers (extended)
    void on_chat_member_updated(ChatMemberHandler handler);
    void on_join_request(JoinRequestHandler handler);
    void on_deleted_messages(DeletedMessagesHandler handler);

    // ---- formatted text (HTML or MarkdownV2 input, parsed into entities)
    Message send_text(const PeerRef& target, const std::string& text, ParseMode mode = ParseMode::None,
                      std::optional<std::int32_t> reply_to_id = std::nullopt,
                      std::optional<std::int64_t> effect_id = std::nullopt);
    Message edit_text(const PeerRef& target, std::int32_t message_id, const std::string& text,
                      ParseMode mode = ParseMode::None);

    // ---- bots: games and inline results
    void set_game_score(const PeerRef& chat, std::int32_t message_id, const PeerRef& user, std::int32_t score,
                        bool force = false, bool edit_message = true);
    std::vector<GameScore> get_game_high_scores(const PeerRef& chat, std::int32_t message_id, const PeerRef& user);
    void answer_inline_results(std::int64_t query_id, const std::vector<InlineResultSpec>& results,
                               std::int32_t cache_time = 0, bool personal = false);

    // ---- shortcuts on a received Message (act on the message's own chat and id)
    Message reply(const Message& to, const std::string& text, ParseMode mode = ParseMode::None);
    Message edit(const Message& target, const std::string& text, ParseMode mode = ParseMode::None);
    void remove(const Message& target);
    void pin(const Message& target, bool silent = false);
    void unpin(const Message& target);
    void react(const Message& target, const std::string& emoji);
    std::vector<Message> forward_to(const Message& source, const PeerRef& to);
    std::vector<Message> copy_to(const Message& source, const PeerRef& to);
    // Votes for the given indices into message.poll->options. Empty list retracts the vote.
    void vote_poll(const Message& poll_message, const std::vector<std::size_t>& option_indexes);

    // ---- folders
    std::vector<FolderInfo> get_folders();
    // Creates a folder with the next free id and returns it.
    FolderInfo create_folder(const FolderInfo& folder);
    // Replaces the folder with the same id.
    void edit_folder(const FolderInfo& folder);
    void delete_folder(std::int32_t folder_id);
    // Folder order as ids; folders not listed keep their relative order at the end.
    void reorder_folders(const std::vector<std::int32_t>& folder_ids);

    // ---- invite links and join requests
    std::vector<std::string> get_invite_links(const PeerRef& chat, std::int32_t limit = 100);
    void delete_invite_link(const PeerRef& chat, const std::string& link);
    // Lists people who joined through a link (or who wait for approval with requested = true).
    std::vector<ChatInviteJoiner> get_invite_joiners(const PeerRef& chat, const std::string& link = "",
                                                     bool requested = false, std::int32_t limit = 100);
    void approve_join_request(const PeerRef& chat, const PeerRef& user, bool approved = true);
    // Approves or declines every pending request, or only those for one invite link.
    void approve_all_join_requests(const PeerRef& chat, bool approved = true, const std::string& link = "");
    // Previews an invite hash (the part after t.me/+) without joining. Phase 16.
    ChatInvitePreview check_chat_invite(const std::string& invite_hash);

    // ---- own star gifts (phase 17). msg_id is the id of the gift's service message in the account's own chat.
    // Shows the gift on the profile (save = true) or hides it (save = false).
    bool save_star_gift(std::int32_t msg_id, bool save = true);
    // Converts a gift into Stars. Returns the server's answer.
    bool convert_star_gift(std::int32_t msg_id);
    // Upgrades a regular gift to a unique one.
    void upgrade_star_gift(std::int32_t msg_id, bool keep_original_details = false);
    // Sends a gift owned by the account to another peer.
    void transfer_star_gift(std::int32_t msg_id, const PeerRef& to);

    // ---- star gift listing (phase 18)
    // Gifts shown on a peer's profile. Pass the returned next_offset to get the next page.
    SavedStarGiftPage get_saved_star_gifts(const PeerRef& peer, const std::string& offset = "",
                                           std::int32_t limit = 100,
                                           std::optional<std::int32_t> collection_id = std::nullopt);
    std::vector<StarGiftCollectionInfo> get_star_gift_collections(const PeerRef& peer);
    UniqueStarGiftInfo get_unique_star_gift(const std::string& slug);
    // Upgrade costs in Stars for a regular gift, one entry per upgrade price tier.
    std::vector<std::int64_t> get_star_gift_upgrade_prices(std::int64_t gift_id);

    // ---- star gift collections (phase 19). msg_ids are own-chat gift message ids.
    StarGiftCollectionInfo create_star_gift_collection(const PeerRef& peer, const std::string& title,
                                                       const std::vector<std::int32_t>& msg_ids);
    // Renames the collection (if title is set) and adds or removes gifts.
    StarGiftCollectionInfo update_star_gift_collection(const PeerRef& peer, std::int32_t collection_id,
                                                       std::optional<std::string> title = std::nullopt,
                                                       const std::vector<std::int32_t>& add_msg_ids = {},
                                                       const std::vector<std::int32_t>& remove_msg_ids = {});
    // ---- invite links (phase 2)
    InviteLinkInfo get_invite_link(const PeerRef& chat, const std::string& link);
    // Lists links created by one admin. Set revoked to true to list revoked links.
    std::vector<InviteLinkInfo> get_admin_invite_links(const PeerRef& chat, const PeerRef& admin, bool revoked = false,
                                                       std::int32_t limit = 100);
    std::vector<AdminInviteCount> get_admins_with_invites(const PeerRef& chat);
    // Changes an existing link. Unset fields stay as they are on the server.
    InviteLinkInfo edit_invite_link(const PeerRef& chat, const std::string& link,
                                    std::optional<std::int32_t> expire_date = std::nullopt,
                                    std::optional<std::int32_t> usage_limit = std::nullopt,
                                    std::optional<bool> request_needed = std::nullopt,
                                    std::optional<std::string> title = std::nullopt);
    // Deletes all revoked links created by one admin.
    void delete_revoked_invite_links(const PeerRef& chat, const PeerRef& admin);

    // ---- messages extras (phase 3)
    std::vector<MessageReadParticipant> get_message_read_participants(const PeerRef& chat, std::int32_t message_id);
    void delete_scheduled_messages(const PeerRef& chat, const std::vector<std::int32_t>& message_ids);
    std::vector<AvailableEffectInfo> get_available_effects();
    // Adds an emoji reaction to a message. big = animated big reaction, add_to_recent = add to recent list.
    void send_reaction_with_options(const PeerRef& chat, std::int32_t message_id, const std::string& emoji,
                                    bool big = false, bool add_to_recent = false);
    // Removes all of your reactions from a message.
    void clear_reaction(const PeerRef& chat, std::int32_t message_id);

    // ---- stories (phase 4)
    std::vector<StoryInfo> get_pinned_stories(const PeerRef& chat, std::int32_t offset_id = 0, std::int32_t limit = 100);
    std::vector<StoryInfo> get_stories_archive(const PeerRef& chat, std::int32_t offset_id = 0, std::int32_t limit = 100);
    std::vector<StoryInfo> get_stories_by_id(const PeerRef& chat, const std::vector<std::int32_t>& story_ids);
    // Returns the ids that were deleted.
    std::vector<std::int32_t> delete_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids);
    // Returns the ids that changed.
    std::vector<std::int32_t> toggle_pinned_stories(const PeerRef& chat, const std::vector<std::int32_t>& story_ids,
                                                    bool pinned);
    std::string export_story_link(const PeerRef& chat, std::int32_t story_id);
    // ---- stories part 2 (phase 20)
    // Marks a peer's stories up to max_id as read. Returns the ids the server marked.
    std::vector<std::int32_t> read_stories(const PeerRef& chat, std::int32_t max_id);
    // Hides or shows every story from peers the account does not follow.
    bool set_all_stories_hidden(bool hidden);
    // Hides or shows one peer's stories in the stories tray.
    bool set_peer_stories_hidden(const PeerRef& chat, bool hidden);
    // Sends an emoji reaction to a story. Set add_to_recent to keep it in recent reactions.
    void send_story_reaction(const PeerRef& chat, std::int32_t story_id, const std::string& emoji,
                             bool add_to_recent = false);

    // ---- folder (chatlist) invite links (phase 5)
    // filter_id is the id of a chat folder. link can be a full URL or just the slug.
    FolderInviteInfo create_folder_invite_link(std::int32_t filter_id, const std::string& title,
                                               const std::vector<PeerRef>& peers);
    std::vector<FolderInviteInfo> get_folder_invite_links(std::int32_t filter_id);
    FolderInviteInfo edit_folder_invite_link(std::int32_t filter_id, const std::string& link,
                                             std::optional<std::string> title = std::nullopt,
                                             std::optional<std::vector<PeerRef>> peers = std::nullopt);
    void delete_folder_invite_link(std::int32_t filter_id, const std::string& link);
    FolderInviteCheck check_folder_invite_link(const std::string& link);
    void join_folder_invite_link(const std::string& link, const std::vector<PeerRef>& peers);

    // ---- forum topics extras (phase 7)
    void set_forum_topic_closed(const PeerRef& channel, std::int32_t topic_id, bool closed = true);
    // The general topic has id 1.
    void set_general_topic_hidden(const PeerRef& channel, bool hidden = true);
    std::vector<ForumTopicInfo> get_forum_topics_by_id(const PeerRef& channel, const std::vector<std::int32_t>& topic_ids);

    // ---- stars gifts and history (phase 8)
    std::vector<StarGiftInfo> get_star_gifts();
    std::vector<StarsTransactionInfo> get_stars_transactions(std::int32_t limit = 50);

    // ---- account settings (phase 9)
    // Days after which an inactive account is deleted (between 30 and 730).
    std::int32_t get_account_ttl_days();
    void set_account_ttl_days(std::int32_t days);
    // True if the username is free and valid for this account.
    bool check_username(const std::string& username);
    GlobalPrivacyInfo get_global_privacy_settings();
    void set_archive_and_mute_new_noncontact(bool enabled);

    // ---- emoji and recent stickers (phase 10)
    void clear_recent_stickers(bool attached = false);
    // Document ids of the default emoji statuses.
    std::vector<std::int64_t> get_default_emoji_status_ids();
    // Returns the document ids that the server found among the given custom emoji ids.
    std::vector<std::int64_t> get_custom_emoji_document_ids(const std::vector<std::int64_t>& ids);

    // ---- quick replies and blocked users (phase 11)
    std::vector<QuickReplyInfo> get_quick_replies();
    // Returns the users you have blocked, most recent first, up to limit.
    std::vector<BlockedUserInfo> get_blocked_users(std::int32_t limit = 100);

    // ---- counts (phase 12)
    std::int32_t get_contacts_count();
    std::int32_t get_dialogs_count();

    // ---- business away and greeting messages (phase 13)
    // Sends the saved quick reply shortcut_id as an away message, always on, to everyone.
    void set_business_away_message(std::int32_t shortcut_id, bool offline_only = false);
    void clear_business_away_message();
    // Sends the saved quick reply shortcut_id to new chats after no_activity_days of silence.
    void set_business_greeting_message(std::int32_t shortcut_id, std::int32_t no_activity_days = 7);
    void clear_business_greeting_message();

    // ---- stealth mode and sticker set edits (phase 14)
    void enable_stealth_mode(bool past = true, bool future = false);
    // Moves a sticker inside its set. Position starts at 0.
    void change_sticker_position(const std::string& short_name, std::int64_t document_id, std::int32_t position);
    // Removes a sticker from its set. The sticker must belong to the set.
    void remove_sticker_from_set(const std::string& short_name, std::int64_t document_id);

    // ---- chat extras (phase 15)
    // Number of members online right now (messages.getOnlines).
    std::int32_t get_chat_online_count(const PeerRef& chat);
    // Number of messages in the chat's history (asks for 1 message and reads the total).
    std::int32_t get_chat_history_count(const PeerRef& chat);
    // Removes the group or channel photo.
    void delete_chat_photo(const PeerRef& chat);

    // ---- boosts and stars (phase 6)
    BoostsStatusInfo get_boosts_status(const PeerRef& chat);
    std::int32_t get_boosters_count(const PeerRef& chat);
    // Your own Stars balance (whole Stars, nanos ignored).
    std::int64_t get_stars_balance();

    // ---- forum topics (supergroups with topics enabled)
    ForumTopicInfo create_forum_topic(const PeerRef& channel, const std::string& title, std::int32_t icon_color = 0);
    std::vector<ForumTopicInfo> get_forum_topics(const PeerRef& channel, std::int32_t limit = 100,
                                                 const std::string& query = "");
    void edit_forum_topic(const PeerRef& channel, std::int32_t topic_id, std::optional<std::string> title = std::nullopt,
                          std::optional<bool> closed = std::nullopt, std::optional<bool> hidden = std::nullopt);
    void pin_forum_topic(const PeerRef& channel, std::int32_t topic_id, bool pinned = true);
    void delete_forum_topic_history(const PeerRef& channel, std::int32_t topic_id);

    // ---- dialogs (paginated)
    // One page of dialogs, newest first. Pass the last page's last top message as the offset to continue.
    std::vector<DialogInfo> get_dialogs(std::int32_t limit = 100, std::int32_t offset_date = 0,
                                        std::int32_t offset_id = 0, std::optional<PeerRef> offset_peer = std::nullopt);
    // Walks all pages up to max_dialogs.
    std::vector<DialogInfo> get_all_dialogs(std::size_t max_dialogs = 1000);

    // ---- chat management (more)
    void mark_unread(const PeerRef& chat, bool unread = true);
    // Moves the chat to the archive folder (1) or back to the main list (0).
    void archive_chat(const PeerRef& chat, bool archived = true);
    // Channels/supergroups. seconds = 0 turns slow mode off.
    void set_slow_mode(const PeerRef& channel, std::int32_t seconds);
    // Default permissions for everyone in a group or channel.
    void set_chat_permissions(const PeerRef& chat, const Restrictions& restrictions);
    // Turns forum topics on or off for a supergroup.
    void enable_forum(const PeerRef& channel, bool enabled = true);

    // ---- typed info
    // Full info for a user, basic group or channel (about, member count). Returns the basic Chat fields too.
    Chat get_chat(const PeerRef& peer);

    // ---- sticker sets
    // Uploads each file as a sticker and creates the set owned by the logged-in account.
    StickerSetInfo create_sticker_set(const std::string& title, const std::string& short_name,
                                      const std::vector<StickerFile>& stickers);
    StickerSetContent get_sticker_set(const std::string& short_name);
    std::vector<StickerSetInfo> get_installed_sticker_sets();
    std::vector<MediaInfo> get_stickers_by_emoji(const std::string& emoji);
    void delete_sticker_set(const std::string& short_name);
    void rename_sticker_set(const std::string& short_name, const std::string& title);
    void remove_sticker_from_set(const MediaInfo& sticker);
    // The sticker must already exist on Telegram (for example one received in a message).
    void add_existing_sticker_to_set(const std::string& short_name, const MediaInfo& sticker, const std::string& emoji);
    void change_sticker_position(const MediaInfo& sticker, std::int32_t position);

    // ---- keyboards, polls, copies, cached media
    Message send_with_markup(const PeerRef& target, const std::string& text,
                             const std::optional<InlineKeyboard>& inline_keyboard,
                             const std::optional<ReplyKeyboard>& reply_keyboard = std::nullopt,
                             std::optional<std::int32_t> reply_to_id = std::nullopt);
    void edit_message_reply_markup(const PeerRef& target, std::int32_t message_id, const InlineKeyboard& keyboard);
    // correct_option < 0 means a regular poll; otherwise a quiz with that zero-based answer.
    Message send_poll(const PeerRef& target, const std::string& question, const std::vector<std::string>& options,
                      bool anonymous = true, bool multiple = false, std::int32_t correct_option = -1,
                      const std::string& explanation = "");
    std::vector<Message> copy_messages(const PeerRef& from, const PeerRef& to,
                                       const std::vector<std::int32_t>& message_ids, bool drop_captions = false);
    // Re-sends a file that Telegram already has (no upload). Use Message::media for the source.
    Message send_cached_media(const PeerRef& target, const MediaInfo& media, const std::string& caption = "");
    Message send_sticker(const PeerRef& target, const Message& source);

    // ---- members, media, edits (more)
    ChatMember get_chat_member(const PeerRef& chat, const PeerRef& user);
    std::vector<ChatMember> get_chat_members(const PeerRef& chat, MemberFilter filter = MemberFilter::Recent,
                                             std::int32_t limit = 200);
    Message send_animation(const PeerRef& target, const std::string& path, const std::string& caption = "");
    Message send_audio(const PeerRef& target, const std::string& path, const std::string& caption = "");
    Message send_video(const PeerRef& target, const std::string& path, const std::string& caption = "");
    Message send_voice(const PeerRef& target, const std::string& path, const std::string& caption = "");
    Message send_contact(const PeerRef& target, const std::string& phone, const std::string& first_name,
                         const std::string& last_name = "");
    Message send_location(const PeerRef& target, double latitude, double longitude);
    Message send_venue(const PeerRef& target, double latitude, double longitude, const std::string& title,
                       const std::string& address);
    Message send_dice(const PeerRef& target, const std::string& emoticon = "\xF0\x9F\x8E\xB2");
    Message edit_message_caption(const PeerRef& target, std::int32_t message_id, const std::string& caption);
    Message edit_message_media(const PeerRef& target, std::int32_t message_id, const std::string& path,
                               const std::string& caption = "");
    void on_edited_message(MessageHandler handler);
    void on_edited_message(Filter filter, MessageHandler handler) {
        on_edited_message([filter = std::move(filter), handler = std::move(handler)](Client& c, const Message& m) {
            if (filter(m)) handler(c, m);
        });
    }
    void on_inline_query(InlineQueryHandler handler);

    // ---- messages (more)
    std::vector<Message> get_scheduled_messages(const PeerRef& chat);
    std::vector<Message> get_discussion_replies(const PeerRef& chat, std::int32_t message_id, std::int32_t limit = 50);
    std::vector<Message> search_global(const std::string& query, std::int32_t limit = 50);
    // Basic groups and private chats only. Channels use channels.deleteHistory (not wrapped yet).
    void delete_chat_history(const PeerRef& chat, bool revoke = false);
    void answer_pre_checkout_query(std::int64_t query_id, bool success, const std::string& error = "");
    // Returns the popup text (or URL) the bot sent back for a callback button.
    std::string request_callback_answer(const PeerRef& chat, std::int32_t message_id, const std::string& data);

    // ---- invite links, titles, admins (more)
    std::string export_chat_invite_link(const PeerRef& chat);
    std::string create_chat_invite_link(const PeerRef& chat, std::optional<std::int32_t> expire_date = std::nullopt,
                                        std::optional<std::int32_t> usage_limit = std::nullopt,
                                        const std::string& title = "", bool request_needed = false);
    void revoke_chat_invite_link(const PeerRef& chat, const std::string& link);
    void set_chat_title(const PeerRef& chat, const std::string& title);
    void promote_chat_member(const PeerRef& chat, const PeerRef& user, const AdminRights& rights,
                             const std::string& rank = "");
    // Channels/supergroups only.
    void restrict_chat_member(const PeerRef& chat, const PeerRef& user, const Restrictions& restrictions,
                              std::int32_t until_date = 0);

    // ---- contacts, account (more)
    void add_contact(const PeerRef& user, const std::string& first_name, const std::string& last_name,
                     const std::string& phone);
    void delete_contacts(const std::vector<PeerRef>& users);
    std::vector<User> get_contacts();
    std::vector<User> import_contacts(const std::vector<ContactInput>& contacts);
    void block_user(const PeerRef& user);
    void unblock_user(const PeerRef& user);
    User update_profile(std::optional<std::string> first_name, std::optional<std::string> last_name,
                        std::optional<std::string> about);
    User set_username(const std::string& username);
    void log_out();

    // ---- bots (more)
    void set_bot_commands(const std::vector<BotCommandSpec>& commands, const std::string& lang_code = "");
    std::vector<BotCommandSpec> get_bot_commands(const std::string& lang_code = "");
    void answer_inline_query(std::int64_t query_id, const std::vector<InlineArticle>& results,
                             std::int32_t cache_time = 0, bool personal = false);

    // ---- messages (extended)
    std::optional<Message> copy_message(const PeerRef& from, const PeerRef& to, std::int32_t message_id);
    std::vector<Message> search_messages(const PeerRef& chat, const std::string& query, std::int32_t limit = 50);
    // Empty emoji removes the reaction.
    void send_reaction(const PeerRef& target, std::int32_t message_id, const std::string& emoji);
    void mark_read(const PeerRef& target, std::int32_t max_id = 0);
    std::vector<Message> get_dialogs_last_messages(std::int32_t limit = 100);

    // ---- users (extended)
    std::vector<User> get_users(const std::vector<PeerRef>& users);

    // ---- chats (extended)
    void create_group(const std::string& title, const std::vector<PeerRef>& users);
    PeerRef create_channel(const std::string& title, const std::string& about = "", bool megagroup = false);
    // Channels/supergroups use channels.editBanned; basic groups use messages.deleteChatUser.
    void ban_chat_member(const PeerRef& chat, const PeerRef& user, std::int32_t until_date = 0);
    // Channels/supergroups only.
    void unban_chat_member(const PeerRef& chat, const PeerRef& user);
    // Basic groups only; channels use leave_channel().
    void leave_group(const PeerRef& chat);

    // ---- chats
    void join_chat_by_invite(const std::string& invite_hash);
    void join_channel(const PeerRef& channel);
    void leave_channel(const PeerRef& channel);

    // ---- profile photos, common chats, sessions
    // Profile photos of a user, newest first. limit caps the total returned (paged internally).
    std::vector<ProfilePhoto> get_profile_photos(const PeerRef& user, int limit = 100);
    // Sets the logged-in account's profile photo from a local image file. Returns the new photo.
    ProfilePhoto set_profile_photo(const std::string& path);
    // Makes an existing profile photo of the account the current one (photos.updateProfilePhoto).
    ProfilePhoto set_profile_photo_from(const ProfilePhoto& photo);
    // Deletes profile photos of the account. Returns the number of photos Telegram removed.
    std::size_t delete_profile_photos(const std::vector<ProfilePhoto>& photos);
    // Groups and channels both the logged-in account and the given user are members of.
    std::vector<Chat> get_common_chats(const PeerRef& user, int limit = 100);
    // All active sessions of the account, the current one included.
    std::vector<SessionInfo> get_sessions();
    // Terminates one session by its hash (account.resetAuthorization). Returns true on success.
    bool terminate_session(std::int64_t hash);

    // ---- stickers (favourites), files, bot menu URL
    // Adds or removes a sticker from the account favourites. The sticker is a message that contains it.
    bool set_favorite_sticker(const Message& sticker, bool favorite);
    bool set_favorite_sticker(const DocumentRef& sticker, bool favorite);
    std::vector<DocumentRef> get_favorite_stickers();
    // Sends a file from save_file(). as_photo sends it as a photo, otherwise as a document. The handle can be reused.
    Message send_uploaded(const PeerRef& target, const UploadedFile& file, bool as_photo,
                          const std::string& caption = "");
    // Uploads a local file once and returns a handle. Use it in place of re-uploading the same file.
    UploadedFile save_file(const std::string& path);
    // Sets the bot menu button to open a URL with the given label.
    bool set_menu_button_url(const std::string& text, const std::string& url);

    // ---- two-step verification (2FA)
    PasswordInfo get_password_info();
    // Checks the 2FA password with SRP (auth.checkPassword). Wrong password throws RpcError PASSWORD_HASH_INVALID.
    bool check_password(const std::string& password);
    // Sets a new 2FA password. current_password is required when one already exists. hint is optional.
    bool set_password(const std::string& current_password, const std::string& new_password,
                      const std::string& hint = "");
    // Removes the 2FA password after checking current_password.
    bool remove_password(const std::string& current_password);

    // ---- privacy (who can see what)
    // Sets who may see a privacy setting. Everyone allows all users, Contacts allows contacts only, Nobody allows no one.
    void set_privacy(PrivacyKey key, PrivacyLevel level);
    // Returns Everyone when any rule allows all, Contacts when contacts are allowed, otherwise Nobody.
    PrivacyLevel get_privacy(PrivacyKey key);

    // ---- sessions as strings, bot profile
    // Exports the saved session as a base64 string (see ClientOptions::session_string). Needs a logged-in client.
    std::string export_session_string() const;
    // Sets the bot name, short description ("about") and long description for a language ("" for default).
    bool set_bot_info(const std::string& lang_code, const std::string& name, const std::string& about,
                      const std::string& description);
    BotInfoText get_bot_info(const std::string& lang_code = "");
    // Resets the bot menu button to the default (commands list).
    bool set_default_menu_button();
    // ---- bot admin extras (phase 21)
    // Removes the default-scope command list for a language ("" = all languages).
    bool reset_bot_commands(const std::string& lang_code = "");
    // Admin rights a bot asks for by default when added to a group, or to a broadcast channel when broadcast = true.
    bool set_bot_default_admin_rights(const AdminRights& rights, bool broadcast = false);

    // ---- connected business bots (phase 22)
    std::vector<ConnectedBotInfo> get_connected_bots();
    // Disconnects a business bot from the account, for all chats.
    void disconnect_connected_bot(const PeerRef& bot);

    // ---- quotes, scheduling, albums, translation, presence
    // Sends text as a reply that quotes `quote_text` from the message `reply_to_id`.
    Message send_quote(const PeerRef& target, const std::string& text, std::int32_t reply_to_id,
                       const std::string& quote_text);
    // Schedules a text message for unix_time (epoch seconds). The server confirms it as a scheduled message.
    void send_scheduled(const PeerRef& target, const std::string& text, std::int64_t unix_time);
    // Sends 2 to 10 local files as one album. The caption goes on the first item.
    std::vector<Message> send_album(const PeerRef& target, const std::vector<std::string>& paths,
                                    const std::string& caption = "");
    // Translates plain text to a language code such as "en" or "hi".
    std::string translate_text(const std::string& text, const std::string& to_lang);
    // Sets the account's online status (false marks it offline).
    void set_online(bool online);

private:
    void require_session() const;
    void drain_updates();
    void handle_update_body(const mtproto::Bytes& body);
    void handle_update(const mtproto::tl::Update& update);
    void dispatch_update_unchecked(const mtproto::tl::Update& update);
    struct DcEndpoint {
        std::int32_t id = 0;
        std::string ip;
        std::int32_t port = 0;
        bool cdn = false;
    };
    std::vector<DcEndpoint> dc_endpoints_;
    std::int64_t download_via_cdn(const mtproto::tl::types::upload_fileCdnRedirect& redirect, std::ofstream& out);
    void handle_membership_update(const mtproto::tl::Update& update);
    std::shared_ptr<mtproto::tl::InputUser> input_user_ptr(PeerRef user) const;
    bool accept_pts(std::int32_t pts, std::int32_t pts_count);
    void apply_update_state(const mtproto::tl::updates_State& state);
    void dispatch_message(const Message& message);
    void cache_users_chats(const std::vector<std::shared_ptr<mtproto::tl::User>>& users,
                           const std::vector<std::shared_ptr<mtproto::tl::Chat>>& chats);
    void ping();
    std::shared_ptr<mtproto::tl::InputFile> upload(const std::string& path, std::string& out_name,
                                                   std::int64_t& out_size);
    std::vector<Message> collect_messages(const mtproto::tl::messages_Messages& result);
    std::optional<Message> find_sent_message(const mtproto::tl::Updates& updates, const PeerRef& target,
                                             const std::string& text, std::int32_t fallback_id);
    Message send_media(const PeerRef& target, std::shared_ptr<mtproto::tl::InputMedia> media,
                       const std::string& caption);

    ClientOptions options_;
    std::unique_ptr<mtproto::Connection> connection_;
    std::unique_ptr<mtproto::MtSession> session_;
    std::unique_ptr<SqliteStorage> storage_;  // set when session_path is a SQLite file
    void persist_state();
    detail::PeerCache cache_;
    struct MessageRoute {
        int group;
        Filter filter;
        MessageHandler handler;
    };
    std::vector<MessageRoute> message_handlers_;
    std::vector<ErrorHandler> error_handlers_;
    void report_handler_error(const std::string& stage, const std::exception& e);
    std::vector<CallbackHandler> callback_handlers_;
    std::vector<RawUpdateHandler> raw_update_handlers_;
    std::vector<MessageHandler> edited_handlers_;
    std::vector<ChatMemberHandler> member_handlers_;
    std::vector<JoinRequestHandler> join_handlers_;
    std::vector<DeletedMessagesHandler> deleted_handlers_;
    std::vector<InlineQueryHandler> inline_handlers_;
    struct UpdateState {
        std::int32_t pts = 0;
        std::int32_t qts = 0;
        std::int32_t date = 0;
        std::int32_t seq = 0;
        bool valid = false;
    };
    UpdateState state_;
    bool running_ = false;
};

}  // namespace cppgram
