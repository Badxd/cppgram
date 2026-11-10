// cppgram - high-level client: connection, updates dispatcher, filters, messages, chats, media.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cppgram/qr_login.h"

#include "cppgram/auth_state.h"
#include "cppgram/async.h"
#include "cppgram/dc.h"
#include "cppgram/errors.h"
#include "cppgram/metrics.h"
#include "cppgram/channel_pts.h"
#include "cppgram/convert.h"
#include "cppgram/filters.h"
#include "cppgram/mtproto/auth.h"
#include "cppgram/mtproto/retry.h"
#include "cppgram/mtproto/socks5.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/parser.h"
#include "cppgram/types.h"

namespace cppgram {

// MTProto proxy (obfuscated transport) for the main DC connection. The secret is hex text: 32 characters for
// a plain secret, or "dd" followed by 32 characters for a padded-intermediate secret. Fake-TLS ("ee") secrets
// are rejected. The secret is never logged.
struct MtprotoProxyOptions {
    // Transport inside the proxy. Auto follows the secret: plain uses intermediate, dd uses padded intermediate.
    // Explicit values are checked against the secret: Padded needs a dd secret, a dd secret needs Auto or Padded,
    // and Abridged needs a plain secret. Mismatches throw std::invalid_argument before any network use.
    enum class Framing { Auto, Intermediate, PaddedIntermediate, Abridged };
    std::string host;
    std::uint16_t port = 443;
    std::string secret;
    int connect_timeout_ms = 10000;
    Framing framing = Framing::Auto;
};

// A fixed address for one Telegram DC. Used before help.getConfig has listed the DCs, for example when a saved
// session belongs to another DC, and for private test servers.
struct DcAddress {
    std::int32_t id = 0;
    std::string ip;
    std::uint16_t port = 443;
};

struct ClientOptions {
    std::int32_t api_id = 0;
    std::string api_hash;
    // Telegram production DC addresses: DC1 149.154.175.53, DC2 149.154.167.51, DC3 149.154.175.100,
    // DC4 149.154.167.91, DC5 91.108.56.130. Port 443 is used.
    std::string dc_ip = "149.154.167.51";
    std::uint16_t dc_port = 443;
    std::int32_t dc_id = 2;
    // Fixed DC addresses, checked first when the client must reach DC N: after a USER_MIGRATE_N or
    // PHONE_MIGRATE_N error, or when a saved session belongs to DC N. Then the addresses from help.getConfig,
    // then the built-in table. Empty by default.
    std::vector<DcAddress> dc_addresses;
    std::string session_path = "cppgram.session";
    // Base64 session from export_session_string(). Used only when no saved session exists yet.
    std::string session_string;
    // SOCKS5 proxy for the main DC connection. Unset: direct TCP. CDN downloads still connect directly.
    std::optional<mtproto::Socks5Options> socks5;
    // MTProto proxy (obfuscated transport) for the main DC connection. Unset: direct TCP. Cannot be combined
    // with socks5 (connect() throws std::invalid_argument if both are set).
    std::optional<MtprotoProxyOptions> mtproto_proxy;
    // RSA public keys used to verify the DC during the first handshake. Empty (the default) uses Telegram's
    // production keys. Only set this for a private DC or a test server that holds its own key.
    std::vector<mtproto::RsaPublicKey> server_keys;
    std::string device_model = "cppgram";
    std::string system_version = "linux";
    std::string app_version = "0.1.0";
    std::string lang_code = "en";
    // Idle time before run() sends a keepalive ping.
    int keepalive_ms = 15000;
    // When run() loses the connection, reconnect with backoff (ClientOptions::retry) and catch up on missed
    // updates. Gives up after max_reconnects consecutive failed attempts. Calls in flight at the drop
    // fail with ConnectionError and are NOT resent.
    bool auto_reconnect = true;
    int max_reconnects = 5;
    // Updates buffered before run() handles them. When full, run() drains the queue before reading more
    // packets (backpressure). Updates are never dropped.
    std::size_t max_pending_updates = 1024;
    // Socket receive timeout used while connecting (TCP + handshake + initConnection).
    // Expiry throws mtproto::ConnectionTimeout instead of blocking forever.
    int connect_timeout_ms = 10000;
    // Retry policy: TCP connect retries with backoff and jitter, and bounded FLOOD_WAIT sleep-and-resend.
    // Defaults: 3 attempts, 500 ms base, 10 s cap, flood waits up to 60 s.
    mtproto::RetryPolicy retry{};
    // Checks each CDN download part against the SHA-256 Telegram sends with it, before writing it to disk.
    bool verify_cdn_hashes = true;
    // Prints each connection stage (TCP, each handshake packet, session setup) to stderr.
    bool debug = false;
    // Metrics registry (see metrics.h). Unset: no Client metrics are recorded. The Client keeps its own handles,
    // so the registry may be shared with other code and scraped with render_prometheus().
    std::shared_ptr<metrics::Registry> metrics;
    // Bytes per upload part: a multiple of 1024, at most 512 KiB. 0 uses 512 KiB, the largest Telegram allows.
    // Smaller parts suit slow links and make UploadProgress fire more often.
    std::uint32_t upload_part_size = 0;
};

class SqliteStorage;

// A file uploaded with Client::save_file. handle is the InputFile reference Telegram returned.
struct UploadedFile {
    std::string name;
    std::int64_t size = 0;
    std::shared_ptr<mtproto::tl::InputFile> handle;
};

// Result of auth.sendCode. delivery names where the code went (for example "app", "sms", "call").
struct LoginCodeInfo {
    std::string phone_code_hash;
    std::string delivery;
    int timeout_seconds = 0;  // 0 when Telegram did not send a timeout
};

// Metric handles used by Client. When ClientOptions::metrics is unset, every handle is a no-op.
struct TransferMetrics {
    metrics::Gauge active;
    metrics::Counter failures;
    metrics::Histogram seconds;
};

struct ClientInstruments {
    metrics::Counter rpc_requests;
    metrics::Counter rpc_successes;
    metrics::CounterFamily rpc_failures;         // label: category
    metrics::Histogram rpc_latency;              // seconds
    metrics::CounterFamily connection_attempts;  // label: dc_id
    metrics::CounterFamily connections;          // label: dc_id
    metrics::Counter disconnects;
    metrics::Counter reconnect_attempts;
    metrics::Gauge connected;                    // 1 while the session is ready, else 0
    metrics::CounterFamily transport_errors;     // label: category
    metrics::Counter auth_failures;
    metrics::Counter flood_waits;
    metrics::Counter upload_bytes;
    metrics::Counter download_bytes;
    metrics::Counter part_retries;               // upload or download parts retried after a transient failure
    TransferMetrics upload;
    TransferMetrics download;
};

namespace detail {
ClientInstruments make_client_instruments(metrics::Registry* registry);

inline double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
} // namespace detail

// One viewer of a story (stories.getStoryViewsList). Forwards and reposts without a user are skipped.
struct StoryViewerInfo {
    std::int64_t user_id = 0;
    std::int32_t date = 0;
    bool blocked = false;
};

struct StoryViewsPage {
    std::int32_t views_count = 0;
    std::int32_t forwards_count = 0;
    std::int32_t reactions_count = 0;
    std::vector<StoryViewerInfo> views;
    std::string next_offset;  // empty when there is no next page
};

// One peer's active stories in stories.getAllStories.
struct PeerStoriesInfo {
    PeerRef peer;
    std::vector<StoryInfo> stories;
};

struct AllStoriesInfo {
    bool not_modified = false;  // the server reported no change since the state passed in
    bool has_more = false;
    std::int32_t count = 0;
    std::string state;          // pass this back to get_all_stories() to fetch only changes
    std::vector<PeerStoriesInfo> peers;
};

// Result of auth.requestPasswordRecovery. email_pattern is a masked address such as "a***@example.com".
struct PasswordRecoveryInfo {
    std::string email_pattern;
};

// A group call (inputGroupCall): its id and access hash.
struct GroupCallRef {
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
};

// One participant of a group call (phone.getGroupParticipants).
struct GroupCallParticipantInfo {
    PeerRef peer;
    std::int32_t date = 0;
    std::optional<std::int32_t> active_date;
    std::int32_t source = 0;
    std::optional<std::int32_t> volume;
    std::string about;
    bool muted = false;
    bool left = false;
    bool can_self_unmute = false;
    bool just_joined = false;
    bool muted_by_you = false;
    bool volume_by_admin = false;
    bool self = false;
    bool video_joined = false;
    bool has_video = false;
    bool has_presentation = false;
    std::optional<std::int64_t> raise_hand_rating;
    std::optional<std::int64_t> paid_stars_total;
};

// Result of phone.getGroupParticipants.
struct GroupCallParticipantsInfo {
    std::int32_t count = 0;
    std::int32_t version = 0;
    std::string next_offset;
    std::vector<GroupCallParticipantInfo> participants;
};

// Sticker set reference for install, uninstall and search calls.
struct StickerSetRef {
    enum class Kind { Id, ShortName, Empty };
    Kind kind = Kind::Id;
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    std::string short_name;
};

// Poll answer for messages.addPollAnswer. option is the answer's bytes from the poll.
struct PollAnswerInfo {
    std::string text;
    mtproto::Bytes option;
};

// Views, forwards and replies of one message (messages.getMessagesViews).
struct MessageViewInfo {
    std::int32_t views = 0;
    std::int32_t forwards = 0;
    std::int32_t replies = 0;
};

// Result of messages.getInlineBotResults. result_count is the number of results in this page.
// A web document (webDocument / webDocumentNoProxy) attached to an inline result: a thumbnail or the content.
// Attributes this client knows are decoded into the fields below. Any other attribute raises SerializationError.
struct WebDocumentInfo {
    std::string url;
    std::string mime_type;
    std::int32_t size = 0;
    std::int64_t access_hash = 0;            // 0 for webDocumentNoProxy, which has no access hash
    std::int32_t width = 0;                  // imageSize or video attribute
    std::int32_t height = 0;
    double duration = 0;                     // video or audio attribute, in seconds
    std::string file_name;                   // filename attribute
    // Attributes this client does not decode, by TL constructor id. They do not fail the result, and they are not
    // dropped silently: the caller can see which constructors were present.
    std::vector<std::uint32_t> unknown_attribute_ids;
};

// Invoice sent as an inline result (inputBotInlineMessageMediaInvoice). Provider data is passed through unchanged.
// This client does not contact the payment provider, and sending an invoice does not complete a payment. A provider
// photo is not supported for sending.
struct InvoiceInfo {
    std::string title;
    std::string description;
    std::string currency;                    // ISO 4217 code, for example "USD"
    std::vector<LabeledPriceInfo> prices;    // at least one
    std::string provider;                    // payment provider name or token from the bot's provider setup
    std::vector<std::uint8_t> payload;       // bot-defined bytes returned with the payment
    std::string provider_data;               // JSON for the provider. Empty sends an empty data object.
    bool test = false;
    bool name_requested = false;
    bool phone_requested = false;
    bool email_requested = false;
    bool shipping_address_requested = false;
    bool flexible = false;
    bool phone_to_provider = false;
    bool email_to_provider = false;
    bool recurring = false;
    std::optional<std::int64_t> max_tip_amount;
    std::optional<std::vector<std::int64_t>> suggested_tip_amounts;
    std::optional<std::string> terms_url;
    std::optional<std::int32_t> subscription_period;
};

// Widget sent as an inline result. Set html for an inline widget, or widget_id for a widget the bot registered
// (inputBotInlineMessageWidgetId). Setting both, or a width, height or public data together with widget_id, throws.
struct WidgetSendInfo {
    std::string html;
    std::int64_t widget_id = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool allow_queries = false;
    bool shared_widget = false;
    std::optional<std::string> public_data;
    std::optional<std::string> chat_data;
};

// Kind of a rich result page block (pageBlock*). Each constructor in layer 233 has its own value. Unsupported is
// pageBlockUnsupported.
enum class RichBlockKind {
    Unsupported, Title, Subtitle, AuthorDate, Header, Subheader, Paragraph, Preformatted, Footer, Kicker, Divider,
    Anchor, List, OrderedList, ListItem, Blockquote, BlockquoteBlocks, Pullquote, Photo, Video, Audio, Document,
    Cover, Embed, EmbedPost, Collage, Slideshow, Channel, Table, Details, RelatedArticles, Map, Math, Thinking, ButtonRow
};

// One cell of a Table block. colspan and rowspan are at least 1.
struct RichTableCell {
    std::string text;               // plain text of the cell, empty when Telegram sent none
    bool header = false;
    bool align_center = false;
    bool align_right = false;
    bool valign_middle = false;
    bool valign_bottom = false;
    std::int32_t colspan = 1;
    std::int32_t rowspan = 1;
};

// One article of a RelatedArticles block. url and webpage_id are always set. The optional fields are the ones
// Telegram may omit.
struct RichRelatedArticle {
    std::string url;
    std::int64_t webpage_id = 0;
    std::string title;
    std::string description;
    std::string author;
    std::optional<std::int64_t> photo_id;
    std::optional<std::int32_t> published_date;  // unix time
};

// One decoded page block of a rich result. Fields a kind does not use keep their defaults. raw holds the full TL
// object, so anything without a public member (table cells, related articles, map geometry, channel) stays reachable.
struct RichBlockInfo {
    RichBlockKind kind = RichBlockKind::Unsupported;
    std::uint32_t constructor_id = 0;   // TL constructor id of the block
    // Plain text. Set for: Title, Subtitle, Header, Subheader, Paragraph, Preformatted, Footer, Kicker, Blockquote,
    // Pullquote, Thinking, Anchor (the anchor name), AuthorDate (the author), EmbedPost (the author), Details, Table,
    // RelatedArticles (the title), Math (the source), ListItem (the item text).
    std::string text;
    std::string caption;                // plain caption of Photo, Video, Audio, Document, Embed, EmbedPost, Collage, Slideshow, Map, Blockquote, Pullquote, BlockquoteBlocks
    std::string url;                    // Embed, EmbedPost, Photo (when the photo links somewhere)
    std::string language;               // Preformatted
    std::string label;                  // ListItem of an OrderedList: its number label, when present
    std::int64_t media_id = 0;          // Photo, Video, Audio, Document: the Telegram id of the media
    bool checkbox = false;              // ListItem
    bool checked = false;               // ListItem
    // Table: rows of cells, and the table's flags.
    std::vector<std::vector<RichTableCell>> rows;
    bool bordered = false;              // Table
    bool striped = false;               // Table
    bool compact = false;               // Table
    // RelatedArticles: the articles in order.
    std::vector<RichRelatedArticle> articles;
    // Map: has_geo is false when Telegram sent no geo point. zoom, width and height are the map's own values.
    bool has_geo = false;
    double latitude = 0;
    double longitude = 0;
    std::int32_t zoom = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    // Channel: the channel or chat the block shows. Forbidden and empty entries leave the fields they do not send at
    // their defaults.
    std::int64_t channel_id = 0;
    std::string channel_title;
    std::string channel_username;
    // Child blocks: Cover (one block), Collage, Slideshow, Details, BlockquoteBlocks, EmbedPost, and the blocks of a
    // ListItem that has no plain text. Items of a List and an OrderedList are ListItem blocks.
    std::vector<RichBlockInfo> items;
    std::shared_ptr<const mtproto::tl::PageBlock> raw;
};

// Kind of the message a bot inline result sends. Unknown covers variants this client does not decode. Rich reports
// the kind, the inline keyboard and the decoded rich blocks (RichBlockInfo).
enum class InlineMessageKind { Text, WebPage, Geo, Venue, Contact, Invoice, Rich, Widget, Unknown };

// One result of messages.getInlineBotResults. media is set for photo and document results. The other fields apply to
// the message kind named in their comment and keep their defaults for other kinds. inline_markup is the inline keyboard
// the bot attached to the message, and is nullopt when there is none.
struct InlineResultInfo {
    std::string id;
    std::string type;  // article, photo, gif, video, document, ...
    std::string title;
    std::string description;
    std::string url;
    std::optional<MediaInfo> media;
    InlineMessageKind message_kind = InlineMessageKind::Unknown;
    std::string message_text;               // Text, WebPage and Widget kinds
    std::vector<TextEntity> entities;       // Text, WebPage and Widget kinds
    bool no_webpage = false;                // Text kind
    bool invert_media = false;              // Text, WebPage and Widget kinds
    std::string web_page_url;               // WebPage kind: the link the message previews
    double latitude = 0;                    // Geo and Venue kinds
    double longitude = 0;
    std::string venue_title;                // Venue kind
    std::string address;                    // Venue kind
    std::string provider;                   // Venue kind
    std::string venue_id;                   // Venue kind
    std::string venue_type;                 // Venue kind
    std::string phone_number;               // Contact kind
    std::string first_name;                 // Contact kind
    std::string last_name;                  // Contact kind
    std::string vcard;                      // Contact kind
    std::string invoice_title;              // Invoice kind
    std::string invoice_description;        // Invoice kind
    std::string currency;                   // Invoice kind, ISO 4217 code
    std::int64_t total_amount = 0;          // Invoice kind, in the currency's smallest units
    bool invoice_test = false;              // Invoice kind
    bool shipping_address_requested = false;  // Invoice kind
    std::int32_t widget_width = 0;          // Widget kind
    std::int32_t widget_height = 0;         // Widget kind
    std::vector<std::uint8_t> widget_payload;  // Widget kind
    std::optional<std::string> public_data;    // Widget kind
    std::optional<std::string> chat_data;      // Widget kind
    std::optional<std::int64_t> widget_id;     // Widget kind, when the widget is registered by id
    std::optional<std::int64_t> public_data_hash;  // Widget kind
    std::optional<InlineKeyboard> inline_markup;  // inline keyboard of the message, when there is one
    std::optional<ReplyKeyboard> reply_markup;    // reply keyboard of the message, when there is one
    std::optional<WebDocumentInfo> thumb;         // every kind that carries a thumbnail
    std::optional<WebDocumentInfo> content;       // botInlineResult content (for example a page or file)
    std::optional<WebDocumentInfo> invoice_photo;  // Invoice kind
    bool rich_rtl = false;                        // Rich kind: right-to-left text
    bool rich_part = false;                       // Rich kind: part of a longer message
    std::vector<MediaInfo> rich_photos;           // Rich kind: photos the body refers to
    std::vector<MediaInfo> rich_documents;        // Rich kind: documents the body refers to
    std::size_t rich_block_count = 0;             // Rich kind: number of top-level page blocks
    std::vector<RichBlockInfo> rich_blocks;       // Rich kind: the top-level page blocks, decoded (see RichBlockInfo)
    // Rich kind: the full decoded TL object, holding every page block. Use it when the block data is needed.
    std::shared_ptr<const mtproto::tl::RichMessage> rich_raw;
};

// Button that opens the bot in a private chat (inlineBotSwitchPM).
struct InlineBotSwitchPMInfo {
    std::string text;
    std::string start_param;  // switch_pm only
    std::string url;          // switch_webview only
};

// Result of messages.getInlineBotResults.
struct InlineBotResultsInfo {
    std::int64_t query_id = 0;
    std::string next_offset;  // empty when there are no more pages
    std::int32_t cache_time = 0;
    bool gallery = false;
    std::optional<InlineBotSwitchPMInfo> switch_pm;
    std::optional<InlineBotSwitchPMInfo> switch_webview;  // text and url of the Web App button
    std::vector<InlineResultInfo> results;
};

// Rich message body (inputRichMessageHTML or inputRichMessageMarkdown). files are the photos and documents the body
// refers to by id. Each file's media must be a cached photo or document.
struct RichMessageFileInfo {
    std::string id;
    // Either a cached photo or document in media, or a local file in path. A path is uploaded first (save_file, then
    // messages.uploadMedia with the bot's own peer), and as_photo says which media type to upload it as. Set one form.
    MediaInfo media;
    std::string path;
    bool as_photo = true;
};

struct RichMessageInput {
    enum class Format { Html, Markdown };
    Format format = Format::Html;
    std::string body;
    bool rtl = false;
    bool noautolink = false;
    std::vector<RichMessageFileInfo> files;
};

// Inline result sent with messages.sendWebViewResultMessage or messages.setBotGuestChatResult. kind selects which
// fields apply: Text uses text, no_webpage and invert_media; WebPage uses text, url and invert_media; Geo uses latitude
// and longitude; Venue adds the venue fields; Contact uses the contact fields; Game uses short_name; Photo and Document
// use media and text as caption; Rich uses rich_message. inline_markup applies to every kind except Game.
// Invoice uses invoice; Widget uses widget. A Game result is not sent through this struct.
struct InlineTextResultInfo {
    enum class Kind { Text, WebPage, Geo, Venue, Contact, Game, Photo, Document, Rich, Invoice, Widget };
    Kind kind = Kind::Text;
    std::string id;
    std::string type;  // e.g. "article", "photo", "game"
    std::string title;
    std::string description;
    std::string text;  // Text message, or caption for Photo and Document
    std::vector<TextEntity> entities;  // formatting of text
    bool no_webpage = false;
    bool invert_media = false;
    double latitude = 0;
    double longitude = 0;
    std::string address;
    std::string provider;
    std::string venue_id;
    std::string venue_type;
    std::string phone_number;
    std::string first_name;
    std::string last_name;
    std::string vcard;
    std::string short_name;  // Game
    MediaInfo media;         // Photo (kind Photo) or Document (kind Document)
    std::string url;         // WebPage
    std::optional<InlineKeyboard> inline_markup;
    std::optional<RichMessageInput> rich_message;  // Rich
    std::optional<InvoiceInfo> invoice;            // Invoice
    std::optional<WidgetSendInfo> widget;          // Widget
    std::optional<ReplyKeyboard> reply_markup;     // reply keyboard, instead of inline_markup (not both)
};


// Inline message of a game or inline query (messages.setInlineGameScore). use_64 selects the 64-bit form.
struct InlineMessageRef {
    bool use_64 = false;
    std::int32_t dc_id = 0;
    std::int64_t owner_id = 0;
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
};

// Who sees a paid reaction. Default follows the account settings, Anonymous hides the sender, Peer shows that peer.
struct PaidReactionPrivacyInfo {
    enum class Kind { Default, Anonymous, Peer };
    Kind kind = Kind::Default;
    PeerRef peer;
};

// Web App link returned by the messages.request*WebView calls.
struct WebViewInfo {
    std::string url;
    bool has_query_id = false;
    std::int64_t query_id = 0;
};

// Parameters for a Web App request that names a bot app. Give either id with access_hash, or bot with short_name.
struct BotAppRef {
    bool by_short_name = false;
    std::int64_t id = 0;
    std::int64_t access_hash = 0;
    PeerRef bot;
    std::string short_name;
};

// Peer settings shown in a chat (messages.getPeerSettings). Only the common flags are read.
// Settings of a peer, from messages.getPeerSettings. Optional fields are set only when Telegram sends them.
struct PeerSettingsInfo {
    bool report_spam = false;
    bool add_contact = false;
    bool block_contact = false;
    bool share_contact = false;
    bool need_contacts_exception = false;
    bool report_geo = false;
    bool autoarchived = false;
    bool invite_members = false;
    bool request_chat_broadcast = false;
    bool business_bot_paused = false;
    bool business_bot_can_reply = false;
    std::optional<std::int32_t> geo_distance;
    std::optional<std::string> request_chat_title;
    std::optional<std::int32_t> request_chat_date;
    std::optional<std::int64_t> business_bot_id;
    std::optional<std::string> business_bot_manage_url;
    std::optional<std::int64_t> charge_paid_message_stars;
    std::optional<std::string> registration_month;
    std::optional<std::string> phone_country;
    std::optional<std::int32_t> name_change_date;
    std::optional<std::int32_t> photo_change_date;
};

// Input of messages.getPeerDialogs: a chat or channel, a folder, or a community. A PeerRef converts implicitly.
struct DialogPeerInput {
    enum class Kind { Peer, Folder, Community };
    Kind kind = Kind::Peer;
    PeerRef peer;               // Peer and Community
    std::int32_t folder_id = 0; // Folder

    DialogPeerInput() = default;
    DialogPeerInput(PeerRef p) : peer(std::move(p)) {}  // NOLINT: implicit by design
    static DialogPeerInput folder(std::int32_t id) {
        DialogPeerInput d;
        d.kind = Kind::Folder;
        d.folder_id = id;
        return d;
    }
    static DialogPeerInput community(PeerRef channel) {
        DialogPeerInput d;
        d.kind = Kind::Community;
        d.peer = std::move(channel);
        return d;
    }
};

// Result of messages.getPeerDialogs: the dialogs, their messages and the account update state.
struct PeerDialogsInfo {
    std::vector<DialogInfo> dialogs;
    std::vector<Message> messages;
    std::int32_t pts = 0;
    std::int32_t qts = 0;
    std::int32_t date = 0;
    std::int32_t seq = 0;
    std::int32_t unread_count = 0;
};

// Options of messages.sendInlineBotResult. Defaults send the result as a plain message.
struct InlineBotSendOptions {
    bool silent = false;
    bool background = false;
    bool clear_draft = false;
    bool hide_via = false;
    std::int32_t schedule_date = 0;
    std::optional<std::int32_t> reply_to_msg_id;
    std::optional<PeerRef> send_as;
    std::optional<std::int32_t> quick_reply_shortcut_id;
    std::optional<std::string> quick_reply_shortcut;  // by shortcut name; used when quick_reply_shortcut_id is unset
    std::optional<std::int64_t> allow_paid_stars;
};

// One message range of the history (messages.getSplitRanges).
struct MessageRangeInfo {
    std::int32_t min_id = 0;
    std::int32_t max_id = 0;
};

// Count of messages matching one filter (messages.getSearchCounters).
struct SearchCounterInfo {
    std::int32_t count = 0;
    bool inexact = false;
};

// A page of saved dialog messages (messages.getSavedDialogs, messages.getSavedDialogsByID).
struct SavedDialogsPage {
    std::int32_t count = 0;
    std::vector<Message> messages;
};

// Result of messages.installStickerSet. archived is true when the set was archived because of the limit.
struct StickerInstallInfo {
    bool archived = false;
    std::size_t archived_sets = 0;
};

// A sticker document with its emoji, from the sticker list calls.
struct StickerDocInfo {
    MediaInfo media;
    std::string emoji;
};

// Stickers of one emoji in a sticker pack (messages.stickerPack).
struct StickerPackInfo {
    std::string emoticon;
    std::vector<std::int64_t> document_ids;
};

// Search keywords of one sticker in a full sticker set (messages.stickerKeyword).
struct StickerKeywordInfo {
    std::int64_t document_id = 0;
    std::vector<std::string> keywords;
};

// A sticker set with its covers. documents, packs and keywords are filled only when Telegram sends the full set.
struct StickerSetCoveredInfo {
    StickerSetInfo set;
    std::vector<MediaInfo> covers;
    std::vector<MediaInfo> documents;
    std::vector<StickerPackInfo> packs;
    std::vector<StickerKeywordInfo> keywords;
};

// Recent stickers or sticker search. not_modified is true when the hash matched and no list was sent.
struct StickerListInfo {
    bool not_modified = false;
    std::int64_t hash = 0;
    std::vector<StickerDocInfo> stickers;
    std::vector<StickerPackInfo> packs;      // recent stickers only
    std::vector<std::int32_t> dates;         // recent stickers only: last-used date, parallel to stickers
    std::optional<std::int32_t> next_offset; // search only
};

// The user's own sticker sets or sticker set search. not_modified is true when the hash matched.
struct StickerSetsPageInfo {
    bool not_modified = false;
    std::int64_t hash = 0;
    std::int32_t count = 0;  // messages.myStickers total; 0 for search
    std::vector<StickerSetCoveredInfo> sets;
};

// Result of messages.composeMessageWithAI and messages.summarizeText. diff_text is the change the server made, when
// it sends one (composeMessageWithAI with proofread or emojify).
struct AiTextInfo {
    std::string text;
    std::vector<TextEntity> entities;
    std::string diff_text;
    std::vector<TextEntity> diff_entities;
};

// Tone for messages.composeMessageWithAI. Named uses a built-in tone name, Id and Slug use a tone saved by the
// account, and SingleUse sends a one-off custom prompt.
struct AiComposeToneInfo {
    enum class Kind { Named, Id, Slug, SingleUse };
    Kind kind = Kind::Named;
    std::string name;           // Named
    std::int64_t id = 0;        // Id
    std::int64_t access_hash = 0;  // Id
    std::string slug;           // Slug
    std::string custom_prompt;  // SingleUse
};

// Options of ephemeral.sendMessage and ephemeral.editMessage. The message needs text, a media, or a rich_message.
// entities apply to the text, or to the caption when media is set. rich_message replaces the text, so text must be
// empty with it. Set at most one of inline_markup and reply_markup. Options marked sendMessage only are rejected by
// ephemeral_edit_message before any request is sent. peer is the chat the message belongs to, when one is needed.
struct EphemeralMessageOptions {
    bool noforwards = false;                      // sendMessage only
    bool invert_media = false;
    bool welcome = false;
    bool anchor = false;                          // sendMessage only
    std::optional<std::int32_t> reply_to_msg_id;  // sendMessage only
    std::optional<PeerRef> peer;
    std::optional<MediaInfo> media;               // cached photo or document
    std::optional<std::string> media_path;        // upload this local file, then send it (see ephemeral_send_message)
    std::optional<UploadedFile> uploaded_media;   // a file already uploaded with save_file or save_file_async
    bool media_as_photo = true;                   // media_path and uploaded_media: photo (true) or document (false)
    std::function<void(std::int64_t done, std::int64_t total)> upload_progress;  // media_path only
    std::optional<InlineKeyboard> inline_markup;
    std::optional<ReplyKeyboard> reply_markup;
    std::optional<RichMessageInput> rich_message;
    std::vector<TextEntity> entities;
};

// Top peer category (contacts.getTopPeers).
enum class TopPeerCategory {
    Correspondents, BotsPM, BotsInline, PhoneCalls, ForwardUsers, ForwardChats, Groups, Channels, BotsApp, BotsGuestChat
};

// One peer of a top peer category, with its rating.
struct TopPeerInfo {
    PeerRef peer;
    double rating = 0;
};

// Peers of one top peer category. count is the server's total for the category.
struct TopPeerCategoryInfo {
    TopPeerCategory category = TopPeerCategory::Correspondents;
    std::int32_t count = 0;
    std::vector<TopPeerInfo> peers;
};

// Discussion message of a channel post (messages.getDiscussionMessage).
struct DiscussionMessageInfo {
    std::vector<Message> messages;
    std::int32_t unread_count = 0;
    std::int32_t max_id = 0;
};

// bots.getAccessSettings result. added_user_ids are the users allowed to use a restricted bot.
struct BotAccessSettingsInfo {
    bool restricted = false;
    std::vector<std::int64_t> added_user_ids;
};

// Menu button of a bot (bots.getBotMenuButton).
struct BotMenuButtonInfo {
    enum class Kind { Default, Commands, Web };
    Kind kind = Kind::Default;
    std::string text;  // Web
    std::string url;   // Web
};

// Answer to a join request sent to a bot (bots.setJoinChatResults).
struct JoinChatBotResultInfo {
    enum class Kind { Approved, Declined, Queued, WebView };
    Kind kind = Kind::Approved;
    std::string url;  // WebView
};

// One event of channels.getAdminLog. action is the decoded TL action, and action_constructor_id is its TL
// constructor id. For message edits and deletions, the messages are also decoded into previous_message,
// new_message and deleted_message.
struct AdminLogEventInfo {
    std::int64_t id = 0;
    std::int32_t date = 0;
    std::int64_t user_id = 0;
    std::uint32_t action_constructor_id = 0;
    std::shared_ptr<mtproto::tl::ChannelAdminLogEventAction> action;
    std::optional<Message> previous_message;  // edit only
    std::optional<Message> new_message;       // edit only
    std::optional<Message> deleted_message;   // delete only
};

// Filter for channels.getAdminLog. Each set flag keeps one kind of event. Unset flags do not filter.
struct AdminLogFilterInfo {
    bool join = false;
    bool leave = false;
    bool invite = false;
    bool ban = false;
    bool unban = false;
    bool kick = false;
    bool unkick = false;
    bool promote = false;
    bool demote = false;
    bool info = false;
    bool settings = false;
    bool pinned = false;
    bool edit = false;
    bool del = false;
    bool group_call = false;
    bool any_set() const {
        return join || leave || invite || ban || unban || kick || unkick || promote || demote || info || settings ||
               pinned || edit || del || group_call;
    }
};

// messages.affectedHistory result: the pts of the affected history, and how many items were removed.
struct AffectedHistoryInfo {
    std::int32_t pts = 0;
    std::int32_t pts_count = 0;
    std::int32_t offset = 0;
};

// Profile tab shown first on a user or channel page (account.setMainProfileTab, channels.setMainProfileTab).
enum class ProfileTab { Posts, Gifts, Media, Files, Music, Voice, Links, Gifs };

// Email verification purpose. LoginSetup and LoginChange need the phone number and its code hash.
struct EmailVerifyPurposeInfo {
    enum class Kind { LoginSetup, LoginChange, Passport };
    Kind kind = Kind::LoginChange;
    std::string phone_number;     // LoginSetup
    std::string phone_code_hash;  // LoginSetup
};

// Proof for account.verifyEmail. Code is the emailed code; Google and Apple carry the provider token.
struct EmailVerificationInfo {
    enum class Kind { Code, Google, Apple };
    Kind kind = Kind::Code;
    std::string value;
};

// account.sentEmailCode result.
struct SentEmailCodeInfo {
    std::string email_pattern;
    std::int32_t length = 0;
};

// Birthday for account.updateBirthday or users.suggestBirthday. year is optional.
struct BirthdayInfo {
    std::int32_t day = 0;
    std::int32_t month = 0;
    std::optional<std::int32_t> year;
};

// Peer color for account.updateColor. Unset fields keep the server default.
struct PeerColorInfo {
    std::optional<std::int32_t> color;
    std::optional<std::int64_t> background_emoji_id;
};

// Emoji status for account.updateEmojiStatus and channels.updateEmojiStatus. Empty clears the status.
struct EmojiStatusInfo {
    enum class Kind { Empty, Emoji };
    Kind kind = Kind::Empty;
    std::int64_t document_id = 0;  // Emoji
    std::optional<std::int32_t> until;
};

// Notification settings for account.updateNotifySettings. Unset fields are left as they are on the server.
struct NotifySettingsInfo {
    std::optional<bool> show_previews;
    std::optional<bool> silent;
    std::optional<std::int32_t> mute_until;  // unix time; 0 unmutes, 2147483647 mutes forever
    std::optional<bool> stories_muted;
    std::optional<bool> stories_hide_sender;
};

// Options for account.initTakeoutSession. Unset flags are not sent.
struct TakeoutOptions {
    bool contacts = false;
    bool message_users = false;
    bool message_chats = false;
    bool message_megagroups = false;
    bool message_channels = false;
    bool files = false;
    std::optional<std::int64_t> file_max_size;
};

// account.getTmpPassword result. The temporary password is valid until valid_until (unix time).
struct TmpPasswordInfo {
    mtproto::Bytes tmp_password;
    std::int32_t valid_until = 0;
};

// Challenge for proving ownership of a TON wallet (wallet.getProofChallenge). Sign the payload with the wallet key.
struct ProofChallengeInfo {
    std::string payload;
    std::int32_t expires = 0;
    std::string domain;
};

// The wallet address a user has published (wallet.getUserAddresses). user_id is set only when has_user is true.
struct WalletUserAddressInfo {
    bool has_user = false;
    std::int64_t user_id = 0;
    std::string address;
    mtproto::Bytes public_key;
};

// Proof that the caller holds the key of an imported wallet. The signature is made outside the library.
struct WalletOwnershipProofInfo {
    std::int32_t timestamp = 0;
    mtproto::Bytes signature;
};

// Replacement for the account's wallet. New creates an empty wallet on the server. Imported uses an existing key and
// needs a proof signed with that key.
struct WalletReplacement {
    enum class Kind { New, Imported };
    Kind kind = Kind::New;
    mtproto::Bytes public_key;                   // Imported
    std::optional<mtproto::Bytes> anchor_public_key;  // Imported, optional
    WalletOwnershipProofInfo proof;              // Imported
};

// wallet.WalletState result. empty is true when the account has no wallet.
struct WalletStateInfo {
    bool empty = true;
    bool creating = false;
    bool backup_enabled = false;
    bool can_export_phrase = false;
    bool can_enable_backup = false;
    std::string address;
    mtproto::Bytes public_key;
    std::int64_t balance = 0;
};

// Where an invoice comes from. Message: a payment button in a chat message. Slug: a payment link.
struct InvoiceRef {
    enum class Kind { Message, Slug };
    Kind kind = Kind::Slug;
    PeerRef peer;               // Message only
    std::int32_t msg_id = 0;    // Message only
    std::string slug;           // Slug only
};


// Shipping option answered to an invoice (messages.setBotShippingResults).
struct ShippingOptionInfo {
    std::string id;
    std::string title;
    std::vector<LabeledPriceInfo> prices;
};

// Which payments.PaymentForm variant was returned.
enum class PaymentFormKind { Invoice, Stars, StarGift };

// Extra payment method offered on a form (paymentFormMethod).
struct PaymentFormMethodInfo {
    std::string url;
    std::string title;
};

// Address the form asks for or saved for the account (postAddress).
struct PostAddressInfo {
    std::string street_line1;
    std::string street_line2;
    std::string city;
    std::string state;
    std::string country_iso2;
    std::string post_code;
};

// Contact and shipping details the account gave before (paymentRequestedInfo).
struct PaymentRequestedInfo {
    std::optional<std::string> name;
    std::optional<std::string> phone;
    std::optional<std::string> email;
    std::optional<PostAddressInfo> shipping_address;
};

// A card saved with the provider (paymentSavedCredentialsCard). Pass id as PaymentCredentials::saved_id.
struct PaymentSavedCredentialInfo {
    std::string id;
    std::string title;
};

// payments.getPaymentForm result. kind says which variant was returned; the invoice flags are set for Invoice and
// the Stars variants carry the same invoice fields. Optional fields are set only when Telegram sends them.
struct PaymentFormInfo {
    PaymentFormKind kind = PaymentFormKind::Invoice;
    std::int64_t form_id = 0;
    std::int64_t bot_id = 0;
    std::int64_t provider_id = 0;
    std::string title;
    std::string description;
    std::string photo_url;  // web document url of the product photo
    std::string currency;
    std::vector<LabeledPriceInfo> prices;
    bool test = false;
    bool name_requested = false;
    bool phone_requested = false;
    bool email_requested = false;
    bool shipping_address_requested = false;
    bool flexible = false;
    bool phone_to_provider = false;
    bool email_to_provider = false;
    bool recurring = false;
    std::optional<std::int64_t> max_tip_amount;
    std::vector<std::int64_t> suggested_tip_amounts;
    std::optional<std::string> terms_url;
    std::optional<std::int32_t> subscription_period;
    std::string url;  // provider payment page
    bool can_save_credentials = false;
    bool password_missing = false;
    std::optional<std::string> native_provider;
    std::optional<std::string> native_params;  // DataJSON text
    std::vector<PaymentFormMethodInfo> additional_methods;
    std::optional<PaymentRequestedInfo> saved_info;
    std::vector<PaymentSavedCredentialInfo> saved_credentials;
};

// Options of payments.sendPaymentForm that answer the form's requests.
struct PaymentSendOptions {
    std::optional<std::string> requested_info_id;   // from PaymentRequestedInfo saved or entered
    std::optional<std::string> shipping_option_id;  // from the shipping options the bot sent
};
// Credentials for payments.sendPaymentForm. Saved reuses a stored card; New sends provider data as JSON text.
struct PaymentCredentials {
    enum class Kind { Saved, New, ApplePay, GooglePay };
    Kind kind = Kind::New;
    std::string saved_id;         // Saved
    mtproto::Bytes tmp_password;  // Saved
    std::string data_json;        // New: provider data. ApplePay: payment_data. GooglePay: payment_token.
    bool save = false;            // New: keep the card for later
};

// Outcome of sendPaymentForm and sendStarsForm. When verification_needed is true the user must open verification_url.
struct PaymentResultInfo {
    bool verification_needed = false;
    std::string verification_url;
};

// payments.checkGiftCode result. Empty peers are reported with has_from / has_to set to false.
struct GiftCodeInfo {
    bool via_giveaway = false;
    bool has_from = false;
    PeerRef from;
    bool has_to = false;
    std::int64_t to_user_id = 0;
    std::int32_t months_or_days = 0;  // the schema field "days"
    std::int32_t date = 0;
    std::int32_t used_date = 0;       // 0 when unused
};

// A star gift owned by an account or a chat. User: msg_id in the owner's history. Chat: saved_id in a chat.
// Slug: a unique gift by its slug.
struct SavedStarGiftRef {
    enum class Kind { User, Chat, Slug };
    Kind kind = Kind::User;
    std::int32_t msg_id = 0;
    PeerRef peer;
    std::int64_t saved_id = 0;
    std::string slug;
};

struct StarsAmountInfo {
    std::int64_t amount = 0;
    std::int32_t nanos = 0;
};

// A page of star gifts (payments.getCraftStarGifts, payments.getResaleStarGifts).
// One attribute of an upgraded gift: a model, a pattern, a backdrop, or the original sender details.
struct StarGiftAttributeInfo {
    enum class Kind { Model, Pattern, Backdrop, OriginalDetails };
    Kind kind = Kind::Model;
    std::string name;
    MediaInfo document;                 // Model and Pattern
    bool crafted = false;               // Model only
    std::int32_t backdrop_id = 0;       // Backdrop only
    std::int32_t center_color = 0;      // Backdrop only
    std::int32_t edge_color = 0;        // Backdrop only
    std::int32_t pattern_color = 0;     // Backdrop only
    std::int32_t text_color = 0;        // Backdrop only
    std::string rarity_tier;            // "permille", "uncommon", "rare", "epic" or "legendary"; empty for OriginalDetails
    std::int32_t rarity_permille = 0;   // permille tier only
    PeerRef sender;                     // OriginalDetails only, when present
    bool has_sender = false;
    PeerRef recipient;                  // OriginalDetails only
    std::int32_t date = 0;              // OriginalDetails only
    std::string message;                // OriginalDetails only
};

// Identifies one attribute for the resale filter and the counters (starGiftAttributeId).
struct StarGiftAttributeIdInfo {
    enum class Kind { Model, Pattern, Backdrop };
    Kind kind = Kind::Model;
    std::int64_t document_id = 0;       // Model and Pattern
    std::int32_t backdrop_id = 0;       // Backdrop
};

// How many resale gifts carry one attribute (starGiftAttributeCounter).
struct StarGiftAttributeCounterInfo {
    StarGiftAttributeIdInfo attribute;
    std::int32_t count = 0;
};

// A page of resale gifts (payments.getResaleStarGifts).
struct ResaleStarGiftsPage {
    std::int32_t count = 0;
    std::vector<StarGiftInfo> gifts;
    std::string next_offset;
    std::vector<StarGiftAttributeInfo> attributes;
    std::int64_t attributes_hash = 0;
    std::vector<StarGiftAttributeCounterInfo> counters;
};

// Bid level of a running star gift auction (auctionBidLevel).
struct StarGiftAuctionBidLevelInfo {
    std::int32_t pos = 0;
    std::int64_t amount = 0;
    std::int32_t date = 0;
};

// One round of a star gift auction (starGiftAuctionRound).
struct StarGiftAuctionRoundInfo {
    std::int32_t num = 0;
    std::int32_t duration = 0;
};

// The account's own bid in a star gift auction (starGiftAuctionUserState).
struct StarGiftAuctionUserStateInfo {
    bool returned = false;
    std::int32_t acquired_count = 0;
    std::optional<std::int64_t> bid_amount;
    std::optional<std::int32_t> bid_date;
    std::optional<std::int64_t> min_bid_amount;
    std::optional<PeerRef> bid_peer;
};

// payments.getStarGiftAuctionState result. state_kind says which fields are filled: Active has the bids and rounds,
// Finished has the final price and listing, and NotModified means the version matched and nothing was sent.
struct StarGiftAuctionStateInfo {
    enum class StateKind { NotModified, Active, Finished };
    StateKind state_kind = StateKind::Active;
    std::optional<StarGiftInfo> gift;
    // Active
    std::int32_t version = 0;
    std::int32_t start_date = 0;
    std::int32_t end_date = 0;
    std::int64_t min_bid_amount = 0;
    std::vector<StarGiftAuctionBidLevelInfo> bid_levels;
    std::vector<std::int64_t> top_bidders;
    std::int32_t next_round_at = 0;
    std::int32_t last_gift_num = 0;
    std::int32_t gifts_left = 0;
    std::int32_t current_round = 0;
    std::int32_t total_rounds = 0;
    std::vector<StarGiftAuctionRoundInfo> rounds;
    // Finished
    std::int64_t average_price = 0;
    std::optional<std::int32_t> listed_count;
    std::optional<std::int32_t> fragment_listed_count;
    std::optional<std::string> fragment_listed_url;
    // Both
    StarGiftAuctionUserStateInfo user_state;
    std::int32_t timeout = 0;
};

// payments.getUniqueStarGiftValueInfo result.
struct UniqueGiftValueInfo {
    bool last_sale_on_fragment = false;
    bool value_is_average = false;
    std::string currency;
    std::int64_t value = 0;
    std::int32_t initial_sale_date = 0;
    std::int64_t initial_sale_stars = 0;
    std::int64_t initial_sale_price = 0;
    std::optional<std::int64_t> floor_price;
    std::optional<std::int64_t> average_price;
    std::optional<std::int32_t> listed_count;
};

// Which star gift auction to read: by gift id or by slug.
struct StarGiftAuctionRef {
    bool by_slug = false;
    std::int64_t gift_id = 0;
    std::string slug;
};

// One Premium boost slot of the account (premium.getMyBoosts). has_peer is false for a slot that is not in use.
struct MyBoostInfo {
    std::int32_t slot = 0;
    bool has_peer = false;
    PeerRef peer;
    std::int32_t date = 0;
    std::int32_t expires = 0;
    std::int32_t cooldown_until_date = 0;  // 0 when the slot has no cooldown
};

// Server app configuration (help.getAppConfig). json is the config tree serialized as text. not_modified is true
// when the hash matched and json is empty.
struct AppConfigInfo {
    std::int32_t hash = 0;
    bool not_modified = false;
    std::string json;
};

// Result of contacts.search: my_results are the caller's own matches first, then global results.
struct ContactSearchResult {
    std::vector<PeerRef> my_results;
    std::vector<PeerRef> results;
};

// Saved music documents of a user (users.getSavedMusic). not_modified is true when the hash matched.
struct SavedMusicPage {
    std::int32_t count = 0;
    bool not_modified = false;
    std::vector<DocumentRef> documents;
};

// Kind of a media area drawn on a story. Only the kinds the library can build are listed.
enum class StoryMediaAreaKind { Url, Weather, StarGift, Location };

// A media area on a story. x, y, w and h are percentages of the frame (0 to 100). rotation is in degrees.
// Only the fields for the chosen kind are read: url for Url, emoji/temperature_c/color for Weather, gift_slug for
// StarGift, latitude/longitude for Location.
struct StoryMediaArea {
    StoryMediaAreaKind kind = StoryMediaAreaKind::Url;
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    double rotation = 0;
    std::string url;
    std::string emoji;
    double temperature_c = 0;
    std::int32_t color = 0;  // ARGB
    std::string gift_slug;
    double latitude = 0;
    double longitude = 0;
};

enum class StoryPrivacyKind { Everyone, Contacts, CloseFriends, Nobody, AllowUsers, DisallowUsers };

// One privacy rule for a story. users is used by AllowUsers and DisallowUsers only and must hold user peers with
// access hashes.
struct StoryPrivacyRule {
    StoryPrivacyKind kind = StoryPrivacyKind::Everyone;
    std::vector<PeerRef> users;
};

// Fields to change with Client::edit_story. Unset optionals keep the current value on the server.
struct StoryEditOptions {
    std::optional<std::string> caption;
    std::string media_path;      // non-empty replaces the story photo or video with this local file
    bool media_as_photo = true;  // false uploads media_path as a video
    std::optional<std::vector<StoryMediaArea>> media_areas;
    std::optional<std::vector<StoryPrivacyRule>> privacy_rules;
    std::optional<DocumentRef> music;
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
    // Called after each uploaded part with the bytes sent so far and the file size.
    using UploadProgress = std::function<void(std::int64_t done, std::int64_t total)>;
    // Connection lifecycle, fired by connect(), disconnect() and the reconnect path.
    enum class ConnectionEvent { Connected, Disconnected, Reconnecting };
    using ConnectionHandler = std::function<void(Client&, ConnectionEvent)>;
    using AuthStateHandler = std::function<void(Client&, AuthState)>;
    // Telegram RPC errors seen by any request, with the bare error name (for example FLOOD_WAIT_30).
    using RpcErrorHandler = std::function<void(Client&, std::int32_t code, const std::string& name)>;
    // stage is the handler kind that threw (for example "message handler"), message is the exception text.
    using ErrorHandler = std::function<void(Client&, const std::string& stage, const std::string& message)>;

    explicit Client(ClientOptions options);

    // Asynchronous operations (see docs/ASYNC.md). Each runs on this client's worker thread, one at a time, in
    // submission order. Submit from one thread at a time, and do not call synchronous methods while one of these
    // is pending. async(f) runs f(client, stop_token) on that thread. The result holds f's return value or its
    // exception. Cancelling before the operation starts means it never runs.
    template <class F>
    auto async(F&& f) -> async::AsyncResult<std::invoke_result_t<F, Client&, std::stop_token>> {
        if (!runner_) runner_ = std::make_unique<async::Runner>();
        return runner_->post([this, fn = std::forward<F>(f)](std::stop_token token) mutable -> decltype(auto) {
            {
                std::lock_guard<std::mutex> lock(stop_mutex_);
                op_token_ = token;
            }
            struct ClearOperation {
                Client& client;
                ~ClearOperation() {
                    std::lock_guard<std::mutex> lock(client.stop_mutex_);
                    client.op_token_.reset();
                }
            } clear{*this};
            return fn(*this, token);
        });
    }
    // Connect, disconnect and bot login on the worker thread. A stop token links the operation to the caller.
    async::AsyncResult<void> connect_async(std::stop_token stop = {});
    async::AsyncResult<void> disconnect_async();
    async::AsyncResult<void> login_bot_async(std::string bot_token, std::stop_token stop = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Opens the connection, creates or loads the auth key, and runs initConnection.
    void connect();
    void disconnect();
    // Safe to call from any thread: reads the atomic flag, not session_, which run() replaces during reconnect.
    bool is_connected() const noexcept { return connected_.load(std::memory_order_acquire); }

    // True from the start of run() until it returns. A stop() issued after this is true always takes effect.
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

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
    // Typed event handlers. A handler that throws is reported through on_error and does not stop the others.
    // Handlers run on the thread that caused the event (the update loop, or the caller of connect/login).
    // Register all handlers before connect() or run(). Registration is not synchronized with event dispatch, so
    // registering from another thread while run() is active is a data race. Handlers are cleared when the client is destroyed.
    void on_connection_state(ConnectionHandler handler);
    void on_auth_state(AuthStateHandler handler);
    void on_rpc_error(RpcErrorHandler handler);
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
    // Stops run() after the current receive returns (at most keepalive_ms later) and interrupts any
    // retry or reconnect backoff in progress. Safe to call from another thread or a handler.
    void stop() noexcept {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        running_ = false;
        stop_source_.request_stop();
    }

    // ---- raw access: any generated request
    template <class Request>
    typename Request::Result invoke(const Request& request) {
        require_session();
        return call_rpc(request);
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
    // Closes a poll the account created so nobody can vote anymore (phase 23).
    void stop_poll(const Message& poll_message);

    // ---- folders
    std::vector<FolderInfo> get_folders();
    // Creates a folder with the next free id and returns it.
    FolderInfo create_folder(const FolderInfo& folder);
    // Replaces the folder with the same id.
    void edit_folder(const FolderInfo& folder);
    void delete_folder(std::int32_t folder_id);
    // Folder order as ids; folders not listed keep their relative order at the end.
    void reorder_folders(const std::vector<std::int32_t>& folder_ids);
    // Folders Telegram suggests adding (messages.getSuggestedDialogFilters), with the server's description.
    std::vector<SuggestedFolderInfo> get_suggested_folders();

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

    // ---- stories part 3 (layer 233 read and view functions)
    // How many stories the account may still post as this peer (stories.canSendStory).
    std::int32_t can_send_story_count(const PeerRef& chat);
    // Records that the account viewed these stories (stories.incrementStoryViews). Returns true on success.
    bool increment_story_views(const PeerRef& chat, const std::vector<std::int32_t>& story_ids);
    // The active stories of one peer, in server order (stories.getPeerStories). Skips deleted or skipped entries.
    std::vector<StoryInfo> get_peer_stories(const PeerRef& chat);
    // A page of viewers for one of the account's own stories (stories.getStoryViewsList).
    // Pass an empty offset for the first page. Set just_contacts to list only contacts.
    StoryViewsPage get_story_views(const PeerRef& chat, std::int32_t story_id, const std::string& offset = "",
                                   std::int32_t limit = 100, bool just_contacts = false);
    // stories.getAllStories: active stories grouped by peer. Pass state from a previous call to fetch only changes.
    AllStoriesInfo get_all_stories(bool next = false, bool hidden = false, const std::string& state = "");
    // stories.editStory with the caption field only. Returns true when the server accepted the edit.
    bool edit_story_caption(const PeerRef& chat, std::int32_t story_id, const std::string& caption);
    // stories.editStory with any of caption, media, media areas, privacy rules and music. Only the fields that are
    // set in options are sent. Throws std::invalid_argument when nothing is set or a rule is incomplete. Returns true
    // when the server accepted the edit; the edited story arrives through the update stream.
    bool edit_story(const PeerRef& chat, std::int32_t story_id, const StoryEditOptions& options);
    // ---- Account settings and auth extras (account.*, auth.*). Calls that return Bool report that Bool.
    // Changes the account phone number. Returns the account user id. Throws RpcError on a bad code.
    std::int64_t change_phone(const std::string& new_phone, const std::string& phone_code_hash, const std::string& code);
    // Sends the code for a phone change. settings are the defaults.
    LoginCodeInfo send_change_phone_code(const std::string& new_phone);
    // Sends a verification code to the account's phone (auth.SentCode).
    LoginCodeInfo send_verify_phone_code(const std::string& phone);
    // auth.checkPaidAuth: checks whether a paid authorization (Premium login) is needed for this phone and form.
    LoginCodeInfo check_paid_auth(const std::string& phone, const std::string& phone_code_hash, std::int64_t form_id);
    SentEmailCodeInfo send_verify_email_code(const EmailVerifyPurposeInfo& purpose, const std::string& email);
    // Verifies the email. Returns the verified email address.
    std::string verify_email(const EmailVerifyPurposeInfo& purpose, const EmailVerificationInfo& verification);
    bool set_authorization_ttl(std::int32_t days);
    bool set_main_profile_tab(ProfileTab tab);
    bool update_birthday(const std::optional<BirthdayInfo>& birthday);
    bool update_color(bool for_profile, const PeerColorInfo& color);
    bool update_emoji_status(const EmojiStatusInfo& status);
    // peer may be a forum topic: pass the topic id in top_msg_id (0 for a whole peer).
    bool update_notify_settings(const PeerRef& peer, std::int32_t top_msg_id, const NotifySettingsInfo& settings);
    bool update_personal_channel(const PeerRef& channel);
    // account.saveMusic: saves (or unsaves) a music document, placing it after after_id when set.
    bool save_music(const DocumentRef& document, bool unsave = false, const std::optional<DocumentRef>& after = std::nullopt);
    // account.initTakeoutSession. Returns the takeout id to pass to invoke_with_takeout.
    std::int64_t init_takeout_session(const TakeoutOptions& options = {});
    bool finish_takeout_session(bool success);
    // account.getTmpPassword: a temporary password for this session, valid for period seconds. Needs the 2FA password.
    TmpPasswordInfo get_tmp_password(const std::string& password, std::int32_t period_seconds);
    bool reset_authorizations();
    // ---- Messages (messages.*), stickers, Web Apps and ephemeral/phone/users extras
    // messages.addChatUser: adds a user to a basic group. Returns how many invitees were not added.
    std::size_t add_chat_user(std::int64_t chat_id, const PeerRef& user, std::int32_t forward_limit = 100);
    bool add_poll_answer(const PeerRef& peer, std::int32_t msg_id, const PollAnswerInfo& answer);
    bool delete_poll_answer(const PeerRef& peer, std::int32_t msg_id, const mtproto::Bytes& option);
    // tone is a built-in tone name; use the AiComposeToneInfo overload for saved or custom tones.
    AiTextInfo compose_message_with_ai(const std::string& text, bool proofread = false, bool emojify = false,
                                       const std::string& translate_to_lang = "", const std::string& tone = "",
                                       const std::vector<TextEntity>& entities = {});
    AiTextInfo compose_message_with_ai(const std::string& text, bool proofread, bool emojify,
                                       const std::string& translate_to_lang, const AiComposeToneInfo& tone,
                                       const std::vector<TextEntity>& entities = {});
    bool delete_participant_reaction(const PeerRef& peer, std::int32_t msg_id, const PeerRef& participant);
    bool delete_participant_reactions(const PeerRef& peer, const PeerRef& participant);
    AffectedHistoryInfo delete_saved_history(const PeerRef& peer, std::int32_t max_id,
                                             const std::optional<PeerRef>& parent_peer = std::nullopt,
                                             std::int32_t min_date = 0, std::int32_t max_date = 0);
    bool edit_chat_about(const PeerRef& peer, const std::string& about);
    // Transfers a group to another member. password is the 2FA password, checked with SRP when the account has one.
    bool edit_chat_creator(const PeerRef& peer, const PeerRef& user, const std::string& password);
    bool edit_chat_participant_rank(const PeerRef& peer, const PeerRef& participant, const std::string& rank);
    std::vector<Chat> get_chats(const std::vector<std::int64_t>& chat_ids);
    DiscussionMessageInfo get_discussion_message(const PeerRef& peer, std::int32_t msg_id);
    InlineBotResultsInfo get_inline_bot_results(const PeerRef& bot, const PeerRef& peer, const std::string& query,
                                                const std::string& offset = "");
    std::vector<MessageViewInfo> get_messages_views(const PeerRef& peer, const std::vector<std::int32_t>& msg_ids,
                                                    bool increment = false);
    StickerSetsPageInfo get_my_stickers(std::int64_t offset_id = 0, std::int32_t limit = 100);
    // messages.getPeerDialogs: returns the messages of the dialogs of these peers, newest first.
    PeerDialogsInfo get_peer_dialogs(const std::vector<DialogPeerInput>& peers);
    PeerSettingsInfo get_peer_settings(const PeerRef& peer);
    std::vector<Message> get_personal_channel_history(const PeerRef& user, std::int32_t limit = 100,
                                                      std::int32_t max_id = 0, std::int32_t min_id = 0,
                                                      std::int64_t hash = 0);
    // Peers of the pinned dialogs of a folder (0 for the main list).
    std::vector<DialogInfo> get_pinned_dialogs(std::int32_t folder_id = 0);
    StickerListInfo get_recent_stickers(bool attached = false, std::int64_t hash = 0);
    SavedDialogsPage get_saved_dialogs(bool exclude_pinned = false, const std::optional<PeerRef>& parent_peer = std::nullopt,
                                       std::int32_t offset_date = 0, std::int32_t offset_id = 0,
                                       const PeerRef& offset_peer = PeerRef{}, std::int32_t limit = 100,
                                       std::int64_t hash = 0);
    SavedDialogsPage get_saved_dialogs_by_id(const std::vector<PeerRef>& peers,
                                             const std::optional<PeerRef>& parent_peer = std::nullopt);
    std::vector<Message> get_saved_history(const PeerRef& peer, std::int32_t offset_id = 0, std::int32_t offset_date = 0,
                                           std::int32_t add_offset = 0, std::int32_t limit = 100,
                                           std::int32_t max_id = 0, std::int32_t min_id = 0, std::int64_t hash = 0,
                                           const std::optional<PeerRef>& parent_peer = std::nullopt);
    std::vector<SearchCounterInfo> get_search_counters(const PeerRef& peer,
                                                       const std::vector<std::shared_ptr<mtproto::tl::MessagesFilter>>& filters,
                                                       const std::optional<std::int32_t>& top_msg_id = std::nullopt);
    std::vector<MessageRangeInfo> get_split_ranges();
    StickerInstallInfo install_sticker_set(const StickerSetRef& set, bool archived = false);
    bool uninstall_sticker_set(const StickerSetRef& set);
    bool reorder_sticker_sets(const std::vector<std::int64_t>& order, bool masks = false, bool emojis = false);
    StickerSetsPageInfo search_sticker_sets(const std::string& query, bool exclude_featured = false, std::int64_t hash = 0);
    StickerListInfo search_stickers(const std::string& query, const std::string& emoticon,
                                    const std::vector<std::string>& lang_codes = {}, bool emojis = false,
                                    std::int32_t offset = 0, std::int32_t limit = 100, std::int64_t hash = 0);
    AffectedHistoryInfo read_mentions(const PeerRef& peer, const std::optional<std::int32_t>& top_msg_id = std::nullopt);
    AffectedHistoryInfo read_reactions(const PeerRef& peer, const std::optional<std::int32_t>& top_msg_id = std::nullopt,
                                       const std::optional<PeerRef>& saved_peer = std::nullopt);
    WebViewInfo request_app_web_view(const PeerRef& peer, const BotAppRef& app, const std::string& start_param = "",
                                     const std::string& platform = "android", bool write_allowed = false);
    WebViewInfo request_main_web_view(const PeerRef& peer, const PeerRef& bot, const std::string& start_param = "",
                                      const std::string& platform = "android");
    WebViewInfo request_simple_web_view(const PeerRef& bot, const std::string& url, const std::string& start_param = "",
                                        const std::string& platform = "android");
    WebViewInfo request_web_view(const PeerRef& peer, const PeerRef& bot, const std::string& url,
                                 const std::string& start_param = "", const std::string& platform = "android");
    bool save_default_send_as(const PeerRef& peer, const PeerRef& send_as);
    bool save_gif(const DocumentRef& gif, bool unsave = false);
    bool send_inline_bot_result(const PeerRef& peer, std::int64_t query_id, const std::string& result_id,
                                const InlineBotSendOptions& options = {});
    bool send_paid_reaction(const PeerRef& peer, std::int32_t msg_id, std::int32_t count,
                            const PaidReactionPrivacyInfo& privacy = {});
    bool send_screenshot_notification(const PeerRef& peer, std::int32_t reply_to_msg_id);
    // Sends a plain text inline result for a Web App query. Returns the inline message id when the server sent one.
    // Returns the inline message the server created, when it sent one.
    std::optional<InlineMessageRef> send_web_view_result_message(const std::string& bot_query_id,
                                                                 const InlineTextResultInfo& result);
    // Returns the inline message id the server assigned to the guest chat result.
    InlineMessageRef set_bot_guest_chat_result(std::int64_t query_id, const InlineTextResultInfo& result);
    bool set_bot_shipping_results(std::int64_t query_id, const std::optional<std::string>& error,
                                  const std::vector<ShippingOptionInfo>& options = {});
    bool set_inline_game_score(const InlineMessageRef& message, const PeerRef& user, std::int32_t score,
                               bool edit_message = false, bool force = false);
    bool start_bot(const PeerRef& bot, const PeerRef& peer, const std::string& start_param);
    AiTextInfo summarize_text(const PeerRef& peer, std::int32_t msg_id, const std::optional<std::string>& to_lang = std::nullopt,
                              const std::optional<std::string>& tone = std::nullopt);
    bool toggle_no_forwards(const PeerRef& peer, bool enabled, const std::optional<std::int32_t>& request_msg_id = std::nullopt);
    bool toggle_suggested_post_approval(const PeerRef& peer, std::int32_t msg_id, bool reject,
                                        std::int32_t schedule_date = 0, const std::optional<std::string>& reject_comment = std::nullopt);
    GroupCallParticipantsInfo get_group_call_participants(const GroupCallRef& call, const std::vector<PeerRef>& ids,
                                                          const std::vector<std::int32_t>& sources,
                                                          const std::string& offset = "", std::int32_t limit = 100);
    // stickers.replaceSticker: swaps a sticker of a set for another document, with its emoji.
    bool replace_sticker(const DocumentRef& sticker, const DocumentRef& new_document, const std::string& emoji);
    // Wraps any request so it runs inside the given message range (invokeWithMessagesRange).
    template <class Query>
    typename Query::Result invoke_with_messages_range(const mtproto::tl::MessageRange& range, const Query& query);
    // ---- Ephemeral messages, sticker edits, birthday and group participants
    bool ephemeral_delete_message(const PeerRef& receiver, std::int32_t msg_id, const std::optional<PeerRef>& peer = std::nullopt);
    bool ephemeral_edit_message(const PeerRef& receiver, std::int32_t msg_id, const std::string& text,
                                const EphemeralMessageOptions& options = {});
    bool ephemeral_send_message(const PeerRef& receiver, const std::string& text,
                                const EphemeralMessageOptions& options = {});
    bool suggest_birthday(const PeerRef& user, const BirthdayInfo& birthday);
    std::vector<TopPeerCategoryInfo> get_top_peers(bool correspondents = false, bool bots_pm = false,
                                                   bool bots_inline = false, bool phone_calls = false,
                                                   bool forward_users = false, bool forward_chats = false,
                                                   bool groups = false, bool channels = false,
                                                   std::int32_t limit = 30);
    bool change_sticker(const DocumentRef& sticker, const std::optional<std::string>& emoji,
                        const std::optional<std::string>& keywords = std::nullopt);
    bool set_sticker_set_thumb(const StickerSetRef& set, const std::optional<DocumentRef>& thumb);
    std::string suggest_sticker_short_name(const std::string& title);
    // ---- Bots (bots.*)
    bool check_bot_username(const std::string& username, bool additional = false);
    // Creates a bot owned by manager. Returns the new bot's user id. Needs the manager to have a session.
    std::int64_t create_bot(const std::string& name, const std::string& username, const PeerRef& manager,
                            bool via_deeplink = false);
    bool edit_bot_access_settings(const PeerRef& bot, bool restricted, const std::vector<PeerRef>& add_users);
    std::string export_bot_token(const PeerRef& bot, bool revoke);
    BotAccessSettingsInfo get_bot_access_settings(const PeerRef& bot);
    // Bots the account manages, as user peers.
    std::vector<PeerRef> get_admined_bots();
    BotMenuButtonInfo get_bot_menu_button(const PeerRef& user);
    bool set_join_chat_results(std::int64_t query_id, const JoinChatBotResultInfo& result);

    // ---- Channels (channels.*) and forum pins
    bool check_channel_username(const PeerRef& channel, const std::string& username);
    bool delete_channel(const PeerRef& channel);
    bool delete_channel_history(const PeerRef& channel, std::int32_t max_id, bool for_everyone = false);
    AffectedHistoryInfo delete_participant_history(const PeerRef& channel, const PeerRef& participant);
    // channels.getAdminLog. Pass min_id 0 and max_id 0 for the newest events.
    std::vector<AdminLogEventInfo> get_admin_log(const PeerRef& channel, const std::string& query = "",
                                                 const AdminLogFilterInfo& filter = {}, std::int64_t max_id = 0,
                                                 std::int64_t min_id = 0, std::int32_t limit = 100);
    std::vector<PeerRef> get_admined_public_channels(bool by_location = false, bool check_limit = false,
                                                     bool for_personal = false);
    // Recommended channels, similar to channel when it is set.
    std::vector<PeerRef> get_channel_recommendations(const std::optional<PeerRef>& channel = std::nullopt);
    std::vector<PeerRef> get_channels(const std::vector<PeerRef>& channels);
    std::vector<PeerRef> get_groups_for_discussion();
    std::vector<PeerRef> get_send_as(const PeerRef& peer, bool for_paid_reactions = false, bool for_live_stories = false);
    // Returns how many invitees the server could not add.
    std::size_t invite_to_channel(const PeerRef& channel, const std::vector<PeerRef>& users);
    std::vector<Message> search_posts(const std::string& hashtag, const std::string& query, std::int32_t offset_rate,
                                      const PeerRef& offset_peer, std::int32_t offset_id, std::int32_t limit = 20,
                                      std::int64_t allow_paid_stars = 0);
    bool set_channel_main_profile_tab(const PeerRef& channel, ProfileTab tab);
    bool update_channel_emoji_status(const PeerRef& channel, const EmojiStatusInfo& status);
    bool update_paid_messages_price(const PeerRef& channel, std::int64_t stars, bool broadcast_messages_allowed = false);
    // ---- Business connections and chatlists
    // account.getBotBusinessConnection: the result arrives as an update (updateBotBusinessConnect). Returns true when sent.
    bool get_bot_business_connection(const std::string& connection_id);
    // chatlists.leaveChatlist: removes the peers from the shared folder with this id. Returns true when accepted.
    bool leave_chatlist(std::int32_t folder_id, const std::vector<PeerRef>& peers);
    // ---- TON wallet (wallet.*). Signatures for imported wallets are made outside the library.
    ProofChallengeInfo get_wallet_proof_challenge();
    std::vector<WalletUserAddressInfo> get_wallet_user_addresses(const std::vector<PeerRef>& users,
                                                                 const std::vector<std::string>& addresses,
                                                                 bool force = false);
    // Replaces the account's wallet. password is needed when the account has a 2FA password, and is then checked with SRP.
    WalletStateInfo replace_wallet(const WalletReplacement& replacement, const std::string& password = "");
    // ---- Payments and Stars (payments.*). Updates-returning calls return true when the server accepted them.
    // theme_params_json is optional DataJSON text for the provider page theme.
    PaymentFormInfo get_payment_form(const InvoiceRef& invoice, const std::string& theme_params_json = "");
    PaymentResultInfo send_payment_form(std::int64_t form_id, const InvoiceRef& invoice,
                                        const PaymentCredentials& credentials, std::int64_t tip_amount = 0,
                                        const PaymentSendOptions& options = {});
    PaymentResultInfo send_stars_form(std::int64_t form_id, const InvoiceRef& invoice);
    // payments.exportInvoice: the invoice_media is an InputMedia built from a received invoice. Returns the URL.
    std::string export_invoice(std::shared_ptr<mtproto::tl::InputMedia> invoice_media);
    GiftCodeInfo check_gift_code(const std::string& slug);
    bool bot_cancel_stars_subscription(const PeerRef& user, const std::string& charge_id, bool restore = false);
    bool change_stars_subscription(const PeerRef& peer, const std::string& subscription_id,
                                   std::optional<bool> canceled = std::nullopt);
    bool fulfill_stars_subscription(const PeerRef& peer, const std::string& subscription_id);
    bool craft_star_gift(const std::vector<SavedStarGiftRef>& gifts);
    bool delete_star_gift(const SavedStarGiftRef& gift);
    bool delete_star_gift_collection(const PeerRef& peer, std::int32_t collection_id);
    bool reorder_star_gift_collections(const PeerRef& peer, const std::vector<std::int32_t>& order);
    bool toggle_star_gifts_pinned_to_top(const PeerRef& peer, const std::vector<SavedStarGiftRef>& gifts);
    bool update_star_gift_price(const SavedStarGiftRef& gift, const StarsAmountInfo& resell_amount);
    bool send_star_gift_offer(const PeerRef& peer, const std::string& slug, const StarsAmountInfo& price,
                              std::int32_t duration_seconds, std::int64_t allow_paid_stars = 0);
    bool resolve_star_gift_offer(std::int32_t offer_msg_id, bool decline);
    bool refund_stars_charge(const PeerRef& user, const std::string& charge_id);
    SavedStarGiftPage get_craft_star_gifts(std::int64_t gift_id, const std::string& offset = "", std::int32_t limit = 100);
    // attributes filters the resale gifts; attributes_hash is the hash of the last page (0 for none).
    ResaleStarGiftsPage get_resale_star_gifts(std::int64_t gift_id, const std::string& offset = "",
                                              std::int32_t limit = 100, bool sort_by_price = false,
                                              bool sort_by_num = false, bool for_craft = false,
                                              bool stars_only = false,
                                              const std::vector<StarGiftAttributeIdInfo>& attributes = {},
                                              std::int64_t attributes_hash = 0);
    // Full auction state for a gift or slug. version is the last version the caller saw (0 for none).
    StarGiftAuctionStateInfo get_star_gift_auction_state(const StarGiftAuctionRef& auction, std::int32_t version);
    // Every upgrade attribute the gift can get (models, patterns, backdrops with rarity).
    std::vector<StarGiftAttributeInfo> get_star_gift_upgrade_attributes(std::int64_t gift_id);
    UniqueGiftValueInfo get_unique_star_gift_value_info(const std::string& slug);
    // ---- Transport wrappers (invokeWith*). Each wraps one request and returns the wrapped request's result.
    template <class Query>
    typename Query::Result invoke_with_takeout(std::int64_t takeout_id, const Query& query);
    template <class Query>
    typename Query::Result invoke_without_updates(const Query& query);
    template <class Query>
    typename Query::Result invoke_with_business_connection(const std::string& connection_id, const Query& query);
    template <class Query>
    typename Query::Result invoke_with_recaptcha(const std::string& token, const Query& query);
    // ---- Premium, app config, contacts and saved music (read-only)
    // premium.getMyBoosts: the account's boost slots, including the ones in use.
    std::vector<MyBoostInfo> get_my_boosts();
    // help.getAppConfig. Pass the hash from a previous call to get not_modified when nothing changed.
    AppConfigInfo get_app_config(std::int32_t hash = 0);
    // contacts.resolvePhone: the account that uses this phone number, or throws RpcError when none is found.
    PeerRef resolve_phone(const std::string& phone);
    // contacts.search: looks up people and chats by name or username. Set broadcasts or bots to narrow the search.
    ContactSearchResult search_contacts(const std::string& query, std::int32_t limit = 20, bool broadcasts = false,
                                        bool bots = false);
    // users.getSavedMusic: music documents saved on a user's profile. Pass hash from a previous page to get not_modified.
    SavedMusicPage get_saved_music(const PeerRef& user, std::int32_t offset = 0, std::int32_t limit = 100,
                                   std::int64_t hash = 0);
    // ---- story posting (phase 27)
    // Posts a photo or video from a local file as a story on the account's own page.
    // period is the lifetime in seconds (6 hours, 12 hours, 1 day or 2 days). Returns the story id.
    std::int32_t post_story(const std::string& path, bool as_photo = true, const std::string& caption = "",
                            std::int32_t period = 86400, bool pinned = false, bool noforwards = false);

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
    // Joins the shared folder through its link. peers are the chats to join with it. Returns true when the server
    // accepted the join; the joined dialogs also arrive as normal updates.
    bool join_folder_invite_link(const std::string& link, const std::vector<PeerRef>& peers);
    // Peers the owner added to a shared folder since the caller last checked (chatlists.getChatlistUpdates).
    FolderUpdatesInfo get_folder_updates(std::int32_t filter_id);
    // Joins the peers the owner added to a shared folder. Returns true when accepted.
    bool join_folder_updates(std::int32_t filter_id, const std::vector<PeerRef>& peers);
    // Hides the pending updates for a shared folder without joining them. Returns true when accepted.
    bool hide_folder_updates(std::int32_t filter_id);
    // Chats the caller can leave to stop being in a shared folder (chatlists.getLeaveChatlistSuggestions).
    std::vector<PeerRef> get_leave_folder_suggestions(std::int32_t filter_id);

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
    // Archives or unarchives several chats in order. Stops at the first failing chat.
    void archive_chats(const std::vector<PeerRef>& chats, bool archived = true);
    // Saves (or removes, when `remove` is true) a sticker in the recent stickers list.
    bool add_recent_sticker(const DocumentRef& sticker);
    bool remove_recent_sticker(const DocumentRef& sticker);
    // Redeems a Premium gift code (the slug from a t.me/giftcode link).
    void apply_gift_code(const std::string& slug);
    // Applies the given boost slots to a channel or group.
    void apply_boost(const PeerRef& chat, const std::vector<std::int32_t>& slots);
    // Unpins every pinned message in a chat, or only those in one topic when top_msg_id is set.
    void unpin_all_messages(const PeerRef& chat, std::optional<std::int32_t> top_msg_id = std::nullopt);
    // Sets a private note on a contact. The user must already be resolved (access hash known).
    void set_contact_note(const PeerRef& user, const std::string& note);
    // Accepts a terms-of-service document. `terms_id` is the JSON id from the server's TermsOfService.
    bool accept_terms_of_service(const std::string& terms_id);
    // ---- batch 7: account and chat settings
    // Default self-destruct period for new accounts, in days (account.getAccountTTL).
    std::int32_t get_account_ttl();
    bool set_account_ttl(std::int32_t days);
    // Auto-delete timer for messages in a chat, in seconds; 0 turns it off (messages.setHistoryTTL).
    void set_chat_ttl(const PeerRef& chat, std::int32_t period_seconds);
    // Public username of a channel or supergroup; empty string removes it (channels.updateUsername).
    bool set_chat_username(const PeerRef& channel, const std::string& username);
    // Links a broadcast channel to its discussion group (channels.setDiscussionGroup).
    bool set_chat_discussion_group(const PeerRef& broadcast, const PeerRef& group);
    // Lets anyone join by sending a message, for a group with a linked channel (channels.toggleJoinToSend).
    void toggle_join_to_send(const PeerRef& channel, bool enabled);
    // Turns forum topics on or off for a supergroup (channels.toggleForum).
    void toggle_forum_topics(const PeerRef& channel, bool enabled, bool tabs = false);
    // Chat name colour in messages (channels.updateColor, profile = false).
    void set_chat_accent_color(const PeerRef& channel, std::int32_t color);
    // Chat colour on the profile page (channels.updateColor, profile = true).
    void set_chat_profile_accent_color(const PeerRef& channel, std::int32_t color);
    // Shows or hides the chat folder tags for the account (messages.toggleDialogFilterTags).
    bool toggle_folder_tags(bool enabled);
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
    // Uploads with per-part progress. Cancelling a save_file_async result stops the upload before the next part.
    UploadedFile save_file(const std::string& path, UploadProgress progress);
    async::AsyncResult<UploadedFile> save_file_async(const std::string& path, UploadProgress progress = {},
                                                     std::stop_token stop = {});
    // Sets the bot menu button to open a URL with the given label.
    bool set_menu_button_url(const std::string& text, const std::string& url);

    // ---- user login state machine (see cppgram/auth_state.h)
    // Current authorization state. Updated by the login methods below and by connect()/disconnect().
    AuthState auth_state() const noexcept { return auth_state_; }
    // Checks whether the loaded session is authorized (users.getUsers on self). Returns true when it is.
    // Unauthorized keys move the state to AuthRevoked; other failures are rethrown after classification.
    bool probe_session();
    // Requests a login code (auth.sendCode). Keep phone_code_hash for sign_in_code(). Phone numbers are
    // never logged. Throws RpcError with the Telegram name (rpc_message()) on failure.
    LoginCodeInfo send_login_code(const std::string& phone_number);
    // Asks Telegram to send the login code again (auth.resendCode). phone_code_hash comes from send_login_code().
    // reason is optional and passed through to the server. Returns delivery "authorized" if the server signed in.
    LoginCodeInfo resend_login_code(const std::string& phone_number, const std::string& phone_code_hash,
                                    const std::string& reason = "");
    // Creates a new account (auth.signUp) after send_login_code() reported that the number has no account.
    // Returns the new user id and moves the state to SignedIn. Throws RpcError on failure.
    std::int64_t sign_up(const std::string& phone_number, const std::string& phone_code_hash,
                         const std::string& first_name, const std::string& last_name,
                         bool no_joined_notifications = false);
    // Starts 2FA password recovery by email (auth.requestPasswordRecovery). Needs a session.
    PasswordRecoveryInfo request_password_recovery();
    // Completes 2FA password recovery with the emailed code (auth.recoverPassword) and returns the user id. Needs a
    // session. When new_password is not empty, the recovery also sets it (account.getPassword gives the KDF
    // parameters, then a new verifier is sent as new_settings). With an empty new_password, the cloud password is
    // cleared. Throws std::invalid_argument on an empty code.
    std::int64_t recover_password(const std::string& code, const std::string& new_password = "",
                                  const std::string& hint = "");
    // Submits the login code (auth.signIn). Returns true when Authorized. Returns false when the account
    // has 2FA: the state is then PasswordRequired and sign_in_password() must be called.
    bool sign_in_code(const std::string& phone_number, const std::string& code, const std::string& phone_code_hash);
    // Submits the 2FA password (SRP). Returns true when Authorized.
    bool sign_in_password(const std::string& password);

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

    // ---- checklists (phase 24)
    // Sends a checklist message. Others may add tasks or complete them when the flags are set.
    void send_todo_list(const PeerRef& chat, const std::string& title, const std::vector<TodoItemInput>& items,
                        bool others_can_append = false, bool others_can_complete = false);
    // Marks tasks of a checklist message done or not done, by task id.
    void toggle_todo_items(const PeerRef& chat, std::int32_t msg_id, const std::vector<std::int32_t>& completed,
                           const std::vector<std::int32_t>& incompleted);
    void append_todo_items(const PeerRef& chat, std::int32_t msg_id, const std::vector<TodoItemInput>& items);

    // ---- inline and receipts (phases 25, 26)
    // Edits a message that a bot sent through inline mode. inline_id is the id the update carries.
    bool edit_inline_message(std::int32_t dc_id, std::int64_t inline_id, std::int64_t access_hash,
                             const std::string& text);
    PaymentReceiptInfo get_payment_receipt(const PeerRef& chat, std::int32_t msg_id);

    // ---- account security, read-only (phase 29). Never changes the password or phone number.
    CloudPasswordStatus get_cloud_password_status();

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

    // ---- QR login and authorization transfer (layer 233). See cppgram/qr_login.h.
    // export_login_token needs api_id and api_hash and no session. import_login_token and export_authorization need a session.
    // import_authorization needs no session but must run on a client connected to the target data center.
    LoginTokenInfo export_login_token();
    LoginTokenInfo import_login_token(const std::vector<std::uint8_t>& token);
    AuthorizationExport export_authorization(std::int32_t dc_id);
    std::int64_t import_authorization(std::int64_t id, const std::vector<std::uint8_t>& bytes);

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
    // Set by migrate_to_dc(). The next connect_impl() ignores any saved key, because an auth key belongs to one DC.
    bool force_new_auth_key_ = false;
    DcEndpoint endpoint_for_dc(std::int32_t dc_id) const;
    void migrate_to_dc(std::int32_t dc_id);
    // DC that issued the phone_code_hash of the current login (0 when no code is pending). sign_in_code() uses
    // the same DC, because a hash is valid only on the DC that issued it.
    std::int32_t login_dc_ = 0;
    // Runs one login request. PHONE_MIGRATE_N and NETWORK_MIGRATE_N move the connection to DC N, and the request
    // is repeated there with the same arguments. The login state is kept across the move (migrate_to_dc() keeps it).
    // At most two moves are made. Any other error is rethrown unchanged.
    template <typename Request>
    typename Request::Result call_login_rpc(const Request& request) {
        constexpr int kMaxLoginMigrations = 2;
        for (int moves = 0;; ++moves) {
            try {
                return call_rpc(request);
            } catch (const mtproto::RpcError& e) {
                const std::int32_t dc = migrate_dc_from_error(e.rpc_message());
                if (dc == 0 || moves >= kMaxLoginMigrations) throw;
                migrate_to_dc(dc);
            }
        }
    }
    std::int64_t download_via_cdn(const mtproto::tl::types::upload_fileCdnRedirect& redirect, std::ofstream& out);
    void handle_membership_update(const mtproto::tl::Update& update);
    std::shared_ptr<mtproto::tl::InputUser> input_user_ptr(PeerRef user) const;
    bool accept_pts(std::int32_t pts, std::int32_t pts_count);
    // Per-channel gate. The first update seen for a channel sets its baseline (pts - pts_count) and is trusted.
    bool accept_channel_pts(std::int64_t channel_id, std::int32_t pts, std::int32_t pts_count);
    // Fetches updates.getChannelDifference for one channel until it is current. Baselines are in-memory only.
    void catch_up_channel(std::int64_t channel_id);
    ChannelPtsTracker channel_pts_;
    // Tears down the dead connection and reconnects with backoff, then catches up. Throws when the budget is spent.
    void reconnect_after_drop();
    mtproto::ReconnectBudget reconnect_budget_{};
    void apply_update_state(const mtproto::tl::updates_State& state);
    void dispatch_message(const Message& message);
    void cache_users_chats(const std::vector<std::shared_ptr<mtproto::tl::User>>& users,
                           const std::vector<std::shared_ptr<mtproto::tl::Chat>>& chats);
    void ping();
    std::shared_ptr<mtproto::tl::InputFile> upload(const std::string& path, std::string& out_name,
                                                   std::int64_t& out_size, const UploadProgress& progress = {});
    // Calls an RPC and resends it after a transient failure: a 5xx server error, a timeout or a network error.
    // Upload and download parts are idempotent on Telegram's side, so a resend is safe. After a timeout or a
    // network error the session is treated as dead: the client reconnects first (same auth key, with the usual
    // backoff and reconnect budget) and then resends. A resend on the dead session could never succeed.
    template <class Request>
    typename Request::Result call_with_part_retry(const Request& request) {
        for (int attempt = 1;; ++attempt) {
            try {
                return call_rpc(request);
            } catch (const std::exception& e) {
                if (!retryable_part_error(e) || !mtproto::may_retry(options_.retry, attempt)) throw;
                instruments_.part_retries.inc();
                if (connection_lost(e)) {
                    if (!options_.auto_reconnect) throw;
                    reconnect_after_drop();  // throws ConnectionError once the reconnect budget is spent
                } else {
                    sleep_part_backoff(attempt);
                }
            }
        }
    }
    static bool retryable_part_error(const std::exception& e);
    static bool connection_lost(const std::exception& e);
    void sleep_part_backoff(int attempt);
    mtproto::tl::upload_File fetch_file_part(const mtproto::tl::functions::upload_getFile& req);
    std::vector<Message> collect_messages(const mtproto::tl::messages_Messages& result);
    // Caches the chats of a messages.Chats result and returns them as peer refs.
    std::vector<PeerRef> chats_as_peers(const mtproto::tl::messages_Chats& result);
    std::vector<Chat> chat_list(const mtproto::tl::messages_Chats& result);
    std::optional<Message> find_sent_message(const mtproto::tl::Updates& updates, const PeerRef& target,
                                             const std::string& text, std::int32_t fallback_id);
    Message send_media(const PeerRef& target, std::shared_ptr<mtproto::tl::InputMedia> media,
                       const std::string& caption);

    ClientOptions options_;
    ClientInstruments instruments_;
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
    std::vector<ConnectionHandler> connection_handlers_;
    std::vector<AuthStateHandler> auth_handlers_;
    std::vector<RpcErrorHandler> rpc_error_handlers_;
    // Single entry point for every RPC: reports Telegram errors to on_rpc_error, then rethrows.
    template <class Request>
    typename Request::Result call_rpc(const Request& request) {
        const auto started = std::chrono::steady_clock::now();
        instruments_.rpc_requests.inc();
        try {
            prepare_session_tokens();
            // The cancel token belongs to this call only. Clear it on every exit so a finished operation cannot
            // cancel later waits, such as the run loop's receive.
            struct ClearCancel {
                mtproto::MtSession* session;
                ~ClearCancel() { session->set_cancel_token(std::stop_token{}); }
            } clear_cancel{session_.get()};
            auto result = session_->call(request);
            instruments_.rpc_successes.inc();
            instruments_.rpc_latency.observe(detail::seconds_since(started));
            return result;
        } catch (const std::exception& e) {
            instruments_.rpc_latency.observe(detail::seconds_since(started));
            instruments_.rpc_failures.labels({std::string(errors::to_string(errors::categorize(e)))}).inc();
            if (const auto* rpc = dynamic_cast<const mtproto::RpcError*>(&e)) {
                notify_rpc_error(rpc->code(), rpc->rpc_message());
            }
            throw;
        }
    }
    void notify_rpc_error(std::int32_t code, const std::string& name);
    void notify_connection(ConnectionEvent event);
    void connect_impl();
    // establish_connection(): drops any open connection, then connect_impl(). It does not touch the login state, so
    // migrate_to_dc() can use it. drop_connection(): closes the session and sets Disconnected, without clearing the
    // pending login. connect() and disconnect() call these and also clear the pending login.
    void establish_connection();
    void drop_connection();
    // Sets the authorization state and notifies on_auth_state when it changes.
    void set_auth_state(AuthState next);
    void recover_login_state() noexcept;
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
    std::atomic<bool> running_{false};
    // Mirrors session_ != nullptr for readers on other threads (is_connected()).
    std::atomic<bool> connected_{false};
    // Guards stop_source_: stop() may be called from any thread while run() replaces the source.
    std::mutex stop_mutex_;
    std::stop_source stop_source_;
    // Token of the async operation running on the worker thread, or nothing. Waits inside that operation use it.
    std::optional<std::stop_token> op_token_;
    std::unique_ptr<async::Runner> runner_;  // created on first async use; destroyed first in ~Client
    std::stop_token current_stop_token();
    // Gives the session this call's stop tokens: the run/operation token for flood-wait sleeps, and the async
    // operation token (if any) for cancelling a wait for a reply. Refreshed per call so no stale token survives.
    void prepare_session_tokens();
    AuthState auth_state_ = AuthState::Unauthenticated;
    // Applies one event; throws std::logic_error when the event is not legal in the current state.
    void apply_auth_event(AuthEvent event);
    // Maps a Telegram RPC error to an auth event and applies it when the transition is legal.
    // Returns the event so callers can decide what to do with it. Never throws.
    AuthEvent note_rpc_error(std::int32_t code, const std::string& message) noexcept;
};

template <class Query>
typename Query::Result Client::invoke_with_takeout(std::int64_t takeout_id, const Query& query) {
    require_session();
    mtproto::tl::functions::invokeWithTakeout<Query> wrapped;
    wrapped.takeout_id = takeout_id;
    wrapped.query = query;
    return call_rpc(wrapped);
}

template <class Query>
typename Query::Result Client::invoke_without_updates(const Query& query) {
    require_session();
    mtproto::tl::functions::invokeWithoutUpdates<Query> wrapped;
    wrapped.query = query;
    return call_rpc(wrapped);
}

template <class Query>
typename Query::Result Client::invoke_with_business_connection(const std::string& connection_id, const Query& query) {
    require_session();
    mtproto::tl::functions::invokeWithBusinessConnection<Query> wrapped;
    wrapped.connection_id = connection_id;
    wrapped.query = query;
    return call_rpc(wrapped);
}

template <class Query>
typename Query::Result Client::invoke_with_recaptcha(const std::string& token, const Query& query) {
    require_session();
    mtproto::tl::functions::invokeWithReCaptcha<Query> wrapped;
    wrapped.token = token;
    wrapped.query = query;
    return call_rpc(wrapped);
}

template <class Query>
typename Query::Result Client::invoke_with_messages_range(const mtproto::tl::MessageRange& range, const Query& query) {
    require_session();
    mtproto::tl::functions::invokeWithMessagesRange<Query> wrapped;
    wrapped.range = std::make_shared<mtproto::tl::MessageRange>(range);
    wrapped.query = query;
    return call_rpc(wrapped);
}

}  // namespace cppgram
