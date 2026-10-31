#include "cppgram/client.h"
#include "cppgram/update_gate.h"

#include <thread>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "cppgram/mtproto/auth.h"
#include "cppgram/session_string.h"
#include "cppgram/storage.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
using mtproto::Bytes;
using mtproto::TlReader;
using mtproto::TlWriter;
namespace tl = mtproto::tl;

namespace {

constexpr std::int32_t kLayer = 232;
constexpr std::size_t kSmallFileLimit = 10u * 1024u * 1024u;
constexpr std::size_t kPartSize = 512u * 1024u;
constexpr std::int32_t kDownloadChunk = 1024 * 1024;

// Adapts a raw packet connection to the handshake's plain-message channel.
void debug_log(bool enabled, const std::string& message) {
    if (enabled) std::cerr << "[cppgram] " << message << "\n";
}

class PlainConnection : public mtproto::PlainChannel {
public:
    PlainConnection(mtproto::Connection& connection, bool debug) : connection_(connection), debug_(debug) {}

    // The handshake layer already wraps (auth_key_id=0, msg_id, length) and unwraps these packets,
    // so this adapter must pass them through untouched.
    void send_plain(const Bytes& packet) override {
        debug_log(debug_, "handshake: sending " + std::to_string(packet.size()) + " bytes");
        connection_.send_packet(packet);
    }

    Bytes receive_plain() override {
        Bytes packet = connection_.receive_packet();
        debug_log(debug_, "handshake: received " + std::to_string(packet.size()) + " bytes");
        return packet;
    }

private:
    mtproto::Connection& connection_;
    bool debug_;
};

std::int64_t random_i64() {
    const Bytes rnd = mtproto::random_bytes(8);
    return static_cast<std::int64_t>(mtproto::le_u64_from(rnd));
}

std::string to_hex(const Bytes& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (const std::uint8_t b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

Bytes read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file: " + path);
    }
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string mime_for(const std::string& name) {
    const auto dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "mp4") return "video/mp4";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "ogg" || ext == "oga") return "audio/ogg";
    if (ext == "pdf") return "application/pdf";
    if (ext == "txt") return "text/plain";
    if (ext == "json") return "application/json";
    return "application/octet-stream";
}

tl::InputPeer self_input_peer() {
    return tl::InputPeer{ty::inputPeerSelf{}};
}

}  // namespace

static double client_jitter01() {
    const mtproto::Bytes rnd = mtproto::random_bytes(8);
    return static_cast<double>(mtproto::le_u64_from(rnd)) / 18446744073709551615.0;
}

Client::Client(ClientOptions options) : options_(std::move(options)) {}

Client::~Client() {
    disconnect();
}

void Client::require_session() const {
    if (!session_) {
        throw std::runtime_error("client is not connected; call connect() first");
    }
}

void Client::connect() {
    disconnect();
    const bool debug = options_.debug;
    connection_ = std::make_unique<mtproto::Connection>(mtproto::TransportKind::Intermediate);
    debug_log(debug, "tcp: connecting to " + options_.dc_ip + ":" + std::to_string(options_.dc_port) +
                         " (dc " + std::to_string(options_.dc_id) + ")");
    // Connect with bounded retries. A refused or timed-out TCP connect is transient; the same connection
    // object cannot be reused after a failed connect, so each attempt gets a fresh Connection.
    mtproto::validate_retry_policy(options_.retry);
    for (int attempt = 1;; ++attempt) {
        try {
            connection_->connect(options_.dc_ip, options_.dc_port);
            break;
        } catch (const mtproto::ConnectionError& e) {
            if (!mtproto::may_retry(options_.retry, attempt)) throw;
            const auto delay = mtproto::backoff_delay(options_.retry, attempt, client_jitter01());
            debug_log(debug, std::string("tcp: connect failed (") + e.what() + "), retrying");
            std::this_thread::sleep_for(delay);
            connection_ = std::make_unique<mtproto::Connection>(mtproto::TransportKind::Intermediate);
        }
    }
    debug_log(debug, "tcp: connected");
    connection_->set_receive_timeout(options_.connect_timeout_ms);

    const std::string& path = options_.session_path;
    const bool sqlite_path = path.ends_with(".sqlite") || path.ends_with(".sqlite3") || path.ends_with(".db");
    storage_.reset();
    if (sqlite_path) storage_ = std::make_unique<SqliteStorage>(path);

    mtproto::SessionData data;
    bool have_key = sqlite_path ? storage_->load_session(data)
                                      : (mtproto::load_session_file(path, data) && data.auth_key.size() == 256);
    if (!have_key && !options_.session_string.empty()) {
        have_key = decode_session_string(options_.session_string, data);
        if (have_key) {
            if (sqlite_path) {
                storage_->save_session(data);
            } else {
                mtproto::save_session_file(path, data);
            }
            debug_log(debug, "session: restored from session_string");
        }
    }
    if (!have_key) {
        debug_log(debug, "handshake: no saved auth key, creating one");
        PlainConnection plain(*connection_, debug);
        mtproto::HandshakeOptions handshake;
        handshake.dc_id = options_.dc_id;
        const mtproto::AuthKeyResult key = mtproto::create_auth_key(plain, handshake);
        data.dc_id = options_.dc_id;
        data.auth_key = key.auth_key;
        data.server_salt = key.server_salt;
        data.time_offset = key.time_offset;
        if (sqlite_path) {
            storage_->save_session(data);
        } else {
            mtproto::save_session_file(path, data);
        }
        debug_log(debug, "handshake: auth key created and saved to " + path);
    } else {
        debug_log(debug, "session: reusing saved auth key from " + path);
    }
    session_ = std::make_unique<mtproto::MtSession>(*connection_, data);
    session_->set_retry_policy(options_.retry);
    session_->set_update_queue_limit(options_.max_pending_updates);
    if (storage_) {
        if (const auto saved = storage_->load_state()) {
            state_.pts = saved->pts;
            state_.qts = saved->qts;
            state_.date = saved->date;
            state_.seq = saved->seq;
            state_.valid = true;
        }
    }

    // initConnection must wrap the first request of a session; help.getConfig is a cheap one.
    fn::invokeWithLayer<fn::initConnection<fn::help_getConfig>> init;
    init.layer = kLayer;
    init.query.api_id = options_.api_id;
    init.query.device_model = options_.device_model;
    init.query.system_version = options_.system_version;
    init.query.app_version = options_.app_version;
    init.query.system_lang_code = options_.lang_code;
    init.query.lang_code = options_.lang_code;
    const tl::Config cfg = session_->call(init);
    dc_endpoints_.clear();
    if (const auto* c = std::get_if<ty::config>(&cfg.value)) {
        for (const auto& opt : c->dc_options) {
            if (!opt) continue;
            const auto* o = std::get_if<ty::dcOption>(&opt->value);
            if (o == nullptr || o->ipv6 || o->secret) continue;
            dc_endpoints_.push_back(DcEndpoint{o->id, o->ip_address, o->port, o->cdn});
        }
    }
    connection_->set_receive_timeout(0);
    debug_log(debug, "session: initConnection done");
}

void Client::disconnect() {
    session_.reset();
    if (connection_) {
        connection_->close();
        connection_.reset();
    }
}

void Client::login_bot(const std::string& bot_token) {
    require_session();
    fn::auth_importBotAuthorization req;
    req.flags = 0;
    req.api_id = options_.api_id;
    req.api_hash = options_.api_hash;
    req.bot_auth_token = bot_token;
    session_->call(req);
    sync_updates_state();
}

void Client::cache_users_chats(const std::vector<std::shared_ptr<tl::User>>& users,
                               const std::vector<std::shared_ptr<tl::Chat>>& chats) {
    cache_.add_users(users);
    cache_.add_chats(chats);
}

User Client::get_me() {
    require_session();
    fn::users_getUsers req;
    req.id = {std::make_shared<tl::InputUser>(tl::InputUser{ty::inputUserSelf{}})};
    const auto users = session_->call(req);
    cache_users_chats(users, {});
    for (const auto& u : users) {
        if (u) {
            if (auto me = detail::user_from_tl(*u); me && me->is_self) {
                return *me;
            }
        }
    }
    throw std::runtime_error("users.getUsers did not return the current user");
}

void Client::on_message(Filter filter, MessageHandler handler, int group) {
    message_handlers_.push_back(MessageRoute{group, std::move(filter), std::move(handler)});
    std::stable_sort(message_handlers_.begin(), message_handlers_.end(),
                     [](const MessageRoute& a, const MessageRoute& b) { return a.group < b.group; });
}

void Client::on_error(ErrorHandler handler) {
    error_handlers_.push_back(std::move(handler));
}

void Client::report_handler_error(const std::string& stage, const std::exception& e) {
    if (error_handlers_.empty()) {
        std::cerr << "cppgram: " << stage << " failed: " << e.what() << "\n";
        return;
    }
    for (auto& handler : error_handlers_) {
        try {
            handler(*this, stage, e.what());
        } catch (const std::exception& inner) {
            std::cerr << "cppgram: error handler failed: " << inner.what() << "\n";
        }
    }
}

void Client::on_callback_query(CallbackHandler handler) {
    callback_handlers_.push_back(std::move(handler));
}

void Client::dispatch_message(const Message& message) {
    // Handlers run group by group. Within a group every matching handler runs, in insertion order.
    for (auto& route : message_handlers_) {
        if (!route.filter(message)) continue;
        try {
            route.handler(*this, message);
        } catch (const std::exception& e) {
            report_handler_error("message handler", e);
        }
    }
}

void Client::handle_update(const tl::Update& update) {
    // Channel updates carry their own per-channel pts, so only the account-wide pts is gated here.
    if (const auto* nm = std::get_if<ty::updateNewMessage>(&update.value)) {
        if (!accept_pts(nm->pts, nm->pts_count)) return;
    } else if (const auto* em = std::get_if<ty::updateEditMessage>(&update.value)) {
        if (!accept_pts(em->pts, em->pts_count)) return;
    } else if (const auto* dm = std::get_if<ty::updateDeleteMessages>(&update.value)) {
        if (!accept_pts(dm->pts, dm->pts_count)) return;
    }
    dispatch_update_unchecked(update);
}

bool Client::accept_pts(std::int32_t pts, std::int32_t pts_count) {
    if (!state_.valid) return true;
    switch (classify_pts(state_.pts, pts, pts_count)) {
        case PtsDecision::Apply:
            state_.pts = pts;
            persist_state();
            return true;
        case PtsDecision::Duplicate:
            return false;
        case PtsDecision::Gap:
            // Some updates were missed. getDifference returns them, so drop this one.
            catch_up();
            return false;
    }
    return false;
}

void Client::apply_update_state(const tl::updates_State& state) {
    const auto& s = std::get<ty::updates_state>(state.value);
    state_.pts = s.pts;
    state_.qts = s.qts;
    state_.date = s.date;
    state_.seq = s.seq;
    state_.valid = true;
    persist_state();
}

void Client::persist_state() {
    if (!storage_ || !state_.valid) return;
    storage_->save_state(StoredUpdateState{state_.pts, state_.qts, state_.date, state_.seq});
}

void Client::sync_updates_state() {
    require_session();
    fn::updates_getState req;
    const tl::updates_State st = invoke(req);
    apply_update_state(st);
}

void Client::catch_up() {
    require_session();
    if (!state_.valid) {
        sync_updates_state();
        return;
    }
    // Each slice moves the state forward; the loop ends on differenceEmpty or a final difference.
    for (;;) {
        fn::updates_getDifference req;
        req.pts = state_.pts;
        req.date = state_.date;
        req.qts = state_.qts;
        const tl::updates_Difference diff = invoke(req);

        if (const auto* empty = std::get_if<ty::updates_differenceEmpty>(&diff.value)) {
            state_.date = empty->date;
            state_.seq = empty->seq;
            state_.valid = true;
            persist_state();
            return;
        }
        if (std::get_if<ty::updates_differenceTooLong>(&diff.value)) {
            // Server dropped too much history: take the current state and continue from there.
            const auto& tl_long = std::get<ty::updates_differenceTooLong>(diff.value);
            state_.pts = tl_long.pts;
            state_.valid = true;
            sync_updates_state();
            continue;
        }
        if (const auto* d = std::get_if<ty::updates_difference>(&diff.value)) {
            cache_users_chats(d->users, d->chats);
            for (const auto& m : d->new_messages) {
                if (!m) continue;
                if (auto msg = detail::message_from_tl(*m, cache_)) dispatch_message(*msg);
            }
            for (const auto& up : d->other_updates) if (up) dispatch_update_unchecked(*up);
            if (d->state) apply_update_state(*d->state);
            return;
        }
        const auto& slice = std::get<ty::updates_differenceSlice>(diff.value);
        cache_users_chats(slice.users, slice.chats);
        for (const auto& m : slice.new_messages) {
            if (!m) continue;
            if (auto msg = detail::message_from_tl(*m, cache_)) dispatch_message(*msg);
        }
        for (const auto& up : slice.other_updates) if (up) dispatch_update_unchecked(*up);
        if (slice.intermediate_state) apply_update_state(*slice.intermediate_state);
    }
}

void Client::dispatch_update_unchecked(const tl::Update& update) {
    handle_membership_update(update);
    for (auto& handler : raw_update_handlers_) {
        try {
            handler(*this, update);
        } catch (const std::exception& e) {
            report_handler_error("raw update handler", e);
        }
    }
    if (const auto* nm = std::get_if<ty::updateNewMessage>(&update.value)) {
        if (nm->message) {
            if (auto msg = detail::message_from_tl(*nm->message, cache_)) dispatch_message(*msg);
        }
    } else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&update.value)) {
        if (ncm->message) {
            if (auto msg = detail::message_from_tl(*ncm->message, cache_)) dispatch_message(*msg);
        }
    } else if (const auto* em = std::get_if<ty::updateEditMessage>(&update.value)) {
        if (em->message) {
            if (auto msg = detail::message_from_tl(*em->message, cache_)) {
                for (auto& handler : edited_handlers_) {
                    try { handler(*this, *msg); } catch (const std::exception& e) {
                        report_handler_error("edited message handler", e);
                    }
                }
            }
        }
    } else if (const auto* ecm = std::get_if<ty::updateEditChannelMessage>(&update.value)) {
        if (ecm->message) {
            if (auto msg = detail::message_from_tl(*ecm->message, cache_)) {
                for (auto& handler : edited_handlers_) {
                    try { handler(*this, *msg); } catch (const std::exception& e) {
                        report_handler_error("edited message handler", e);
                    }
                }
            }
        }
    } else if (const auto* iq = std::get_if<ty::updateBotInlineQuery>(&update.value)) {
        InlineQuery query;
        query.id = iq->query_id;
        query.user_id = iq->user_id;
        query.query = iq->query;
        query.offset = iq->offset;
        for (auto& handler : inline_handlers_) {
            try { handler(*this, query); } catch (const std::exception& e) {
                report_handler_error("inline query handler", e);
            }
        }
    } else if (const auto* cb = std::get_if<ty::updateBotCallbackQuery>(&update.value)) {
        CallbackQuery query;
        query.id = cb->query_id;
        query.user_id = cb->user_id;
        query.message_id = cb->msg_id;
        if (cb->data) query.data.assign(cb->data->begin(), cb->data->end());
        if (cb->peer) query.chat = detail::peer_from_tl(*cb->peer, cache_);
        for (auto& handler : callback_handlers_) {
            try {
                handler(*this, query);
            } catch (const std::exception& e) {
                report_handler_error("callback handler", e);
            }
        }
    }
}

void Client::handle_update_body(const Bytes& body) {
    TlReader r(body);
    const std::uint32_t id = r.read_uint32();
    switch (id) {
        case ty::updateShortMessage::kId: {
            const ty::updateShortMessage m = ty::updateShortMessage::read_body(r);
            if (!accept_pts(m.pts, m.pts_count)) return;
            PeerRef chat;
            chat.kind = PeerKind::User;
            chat.id = m.user_id;
            if (const auto known = cache_.find(PeerKind::User, m.user_id)) chat = *known;
            PeerRef sender = chat;
            if (m.out) sender = PeerRef{PeerKind::User, cache_.self_id(), 0, false};
            dispatch_message(detail::short_message(m.id, chat, sender, m.message, m.date, m.out));
            return;
        }
        case ty::updateShortChatMessage::kId: {
            const ty::updateShortChatMessage m = ty::updateShortChatMessage::read_body(r);
            if (!accept_pts(m.pts, m.pts_count)) return;
            PeerRef chat{PeerKind::Chat, m.chat_id, 0, false};
            PeerRef sender{PeerKind::User, m.from_id, 0, false};
            if (const auto known = cache_.find(PeerKind::User, m.from_id)) sender = *known;
            dispatch_message(detail::short_message(m.id, chat, sender, m.message, m.date, m.out));
            return;
        }
        case ty::updateShort::kId: {
            const ty::updateShort s = ty::updateShort::read_body(r);
            if (s.update) handle_update(*s.update);
            return;
        }
        case ty::updates::kId: {
            const ty::updates u = ty::updates::read_body(r);
            cache_users_chats(u.users, u.chats);
            for (const auto& up : u.updates) if (up) handle_update(*up);
            return;
        }
        case ty::updatesCombined::kId: {
            const ty::updatesCombined u = ty::updatesCombined::read_body(r);
            cache_users_chats(u.users, u.chats);
            for (const auto& up : u.updates) if (up) handle_update(*up);
            return;
        }
        case ty::updatesTooLong::kId:
            catch_up();
            return;
        default:
            return;
    }
}

void Client::drain_updates() {
    if (!session_) return;
    for (const Bytes& body : session_->take_updates()) {
        try {
            handle_update_body(body);
        } catch (const std::exception& e) {
            std::cerr << "cppgram: failed to process update: " << e.what() << "\n";
        }
    }
}

void Client::ping() {
    fn::ping_delay_disconnect req;
    req.ping_id = random_i64();
    req.disconnect_delay = 75;
    TlWriter w;
    req.serialize(w);
    session_->send_content(w.bytes());
}

void Client::reconnect_after_drop() {
    for (;;) {
        if (!reconnect_budget_.record_failure()) {
            throw mtproto::ConnectionError("gave up reconnecting after " + std::to_string(options_.max_reconnects) +
                                           " failed attempts");
        }
        session_.reset();
        if (connection_) {
            connection_->close();
            connection_.reset();
        }
        std::this_thread::sleep_for(
            mtproto::backoff_delay(options_.retry, reconnect_budget_.failures, client_jitter01()));
        try {
            connect();
            connection_->set_receive_timeout(options_.keepalive_ms);
            catch_up();  // recover updates missed while disconnected
            return;
        } catch (const mtproto::ConnectionError&) {
            // Still down: loop, which spends one more unit of the budget.
        }
    }
}

void Client::run() {
    require_session();
    running_ = true;
    // Restored state (SQLite session) means updates may have been missed while offline.
    if (storage_ && state_.valid) catch_up();
    connection_->set_receive_timeout(options_.keepalive_ms);
    try {
        reconnect_budget_ = mtproto::ReconnectBudget{options_.max_reconnects, 0};
        while (running_) {
            drain_updates();
            // Backpressure: do not read more packets while the update queue is at its limit.
            while (session_->update_queue_full()) drain_updates();
            try {
                session_->process_one();
                reconnect_budget_.reset();
            } catch (const mtproto::ConnectionTimeout&) {
                ping();
            } catch (const mtproto::ConnectionError&) {
                if (!options_.auto_reconnect) throw;
                reconnect_after_drop();
            }
        }
        drain_updates();
    } catch (...) {
        if (connection_) connection_->set_receive_timeout(0);
        running_ = false;
        throw;
    }
    if (connection_) connection_->set_receive_timeout(0);
    running_ = false;
}

namespace {

// Accepts "@name", "name", "https://t.me/name", "t.me/name?start=x", and returns the bare reference.
std::string strip_reference(std::string ref) {
    while (!ref.empty() && std::isspace(static_cast<unsigned char>(ref.front()))) ref.erase(0, 1);
    while (!ref.empty() && std::isspace(static_cast<unsigned char>(ref.back()))) ref.pop_back();
    for (const char* prefix : {"https://t.me/", "http://t.me/", "t.me/", "https://telegram.me/", "telegram.me/"}) {
        const std::string p(prefix);
        if (ref.rfind(p, 0) == 0) {
            ref.erase(0, p.size());
            break;
        }
    }
    if (!ref.empty() && ref[0] == '@') ref.erase(0, 1);
    return ref;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

PeerRef Client::resolve(const std::string& reference) {
    require_session();
    std::string ref = strip_reference(reference);
    if (ref.empty()) throw std::invalid_argument("resolve: empty reference");

    if (ref == "me" || ref == "self") {
        if (cache_.self_id() == 0) get_me();
        const auto self = cache_.find(PeerKind::User, cache_.self_id());
        return self.value_or(PeerRef{PeerKind::User, cache_.self_id(), 0, false});
    }

    // Message links to private channels: t.me/c/<channel id>/<message id>.
    if (ref.rfind("c/", 0) == 0) {
        const std::size_t end = ref.find('/', 2);
        const std::string id = ref.substr(2, end == std::string::npos ? std::string::npos : end - 2);
        if (!all_digits(id)) throw std::invalid_argument("resolve: bad channel link " + reference);
        ref = "-100" + id;
    }

    // Numeric references: -100<id> channel, -<id> basic group, <id> user (must already be cached).
    const bool negative = ref.size() > 1 && ref[0] == '-';
    const std::string digits = negative ? ref.substr(1) : ref;
    if (all_digits(digits)) {
        if (negative) {
            if (digits.size() > 3 && digits.rfind("100", 0) == 0) {
                const std::int64_t id = std::stoll(digits.substr(3));
                if (auto known = cache_.find(PeerKind::Channel, id)) return *known;
                throw std::runtime_error("channel " + ref + " is not cached; resolve it by username or invite link first");
            }
            return PeerRef{PeerKind::Chat, std::stoll(digits), 0, false};
        }
        const std::int64_t id = std::stoll(digits);
        if (auto known = cache_.find(PeerKind::User, id)) return *known;
        throw std::runtime_error("user " + ref + " is not cached; resolve it by username first");
    }

    // Usernames are case-insensitive and may be followed by "/..." or "?..." in links.
    const std::size_t cut = ref.find_first_of("/?");
    const std::string name = lowercase(cut == std::string::npos ? ref : ref.substr(0, cut));
    if (auto known = cache_.find_username(name)) return *known;

    fn::contacts_resolveUsername req;
    req.username = name;
    const tl::contacts_ResolvedPeer resolved = session_->call(req);
    const auto* rp = std::get_if<ty::contacts_resolvedPeer>(&resolved.value);
    if (rp == nullptr || !rp->peer) {
        throw std::runtime_error("username not found: " + name);
    }
    cache_users_chats(rp->users, rp->chats);
    return detail::peer_from_tl(*rp->peer, cache_);
}

tl::InputPeer Client::input_peer(const PeerRef& peer) const {
    switch (peer.kind) {
        case PeerKind::User:
            if (peer.id == cache_.self_id()) return self_input_peer();
            if (!peer.has_access_hash) {
                throw std::runtime_error("user access hash unknown; resolve the user first");
            }
            return tl::InputPeer{ty::inputPeerUser{peer.id, peer.access_hash}};
        case PeerKind::Chat:
            return tl::InputPeer{ty::inputPeerChat{peer.id}};
        case PeerKind::Channel:
            if (!peer.has_access_hash) {
                throw std::runtime_error("channel access hash unknown; resolve the channel first");
            }
            return tl::InputPeer{ty::inputPeerChannel{peer.id, peer.access_hash}};
    }
    throw std::runtime_error("unknown peer kind");
}

std::vector<Message> Client::collect_messages(const tl::messages_Messages& result) {
    std::vector<Message> out;
    std::visit(
        [&](const auto& v) {
            if constexpr (requires { v.users; v.chats; v.messages; }) {
                cache_users_chats(v.users, v.chats);
                for (const auto& m : v.messages) {
                    if (!m) continue;
                    if (auto msg = detail::message_from_tl(*m, cache_)) out.push_back(*msg);
                }
            }
        },
        result.value);
    return out;
}

std::optional<Message> Client::find_sent_message(const tl::Updates& updates, const PeerRef& target,
                                                 const std::string& text, std::int32_t fallback_id) {
    if (const auto* s = std::get_if<ty::updateShortSentMessage>(&updates.value)) {
        return detail::short_message(s->id, target, PeerRef{PeerKind::User, cache_.self_id(), 0, false}, text,
                                     s->date, true);
    }
    const std::vector<std::shared_ptr<tl::Update>>* list = nullptr;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        cache_users_chats(u->users, u->chats);
        list = &u->updates;
    } else if (const auto* c = std::get_if<ty::updatesCombined>(&updates.value)) {
        cache_users_chats(c->users, c->chats);
        list = &c->updates;
    }
    if (list != nullptr) {
        for (const auto& up : *list) {
            if (!up) continue;
            const tl::Message* raw = nullptr;
            if (const auto* nm = std::get_if<ty::updateNewMessage>(&up->value)) raw = nm->message.get();
            else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) raw = ncm->message.get();
            if (raw != nullptr) {
                if (auto msg = detail::message_from_tl(*raw, cache_)) return msg;
            }
        }
    }
    if (fallback_id != 0) {
        return detail::short_message(fallback_id, target, std::nullopt, text, 0, true);
    }
    return std::nullopt;
}

Message Client::send_message(const std::string& target, const std::string& text,
                             std::optional<std::int32_t> reply_to_id) {
    return send_message(resolve(target), text, reply_to_id);
}

Message Client::send_message(const PeerRef& target, const std::string& text,
                             std::optional<std::int32_t> reply_to_id, std::optional<std::int64_t> effect_id) {
    require_session();
    fn::messages_sendMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.message = text;
    req.random_id = random_i64();
    if (reply_to_id) {
        req.reply_to = std::make_shared<tl::InputReplyTo>(tl::InputReplyTo{ty::inputReplyToMessage{*reply_to_id}});
    }
    req.effect = effect_id;
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, text, 0)) {
        return *msg;
    }
    throw std::runtime_error("message was sent but no id was returned by the server");
}

Message Client::edit_message(const PeerRef& target, std::int32_t message_id, const std::string& text) {
    require_session();
    fn::messages_editMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.message = text;
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, text, message_id)) {
        return *msg;
    }
    return detail::short_message(message_id, target, std::nullopt, text, 0, true);
}

void Client::delete_messages(const PeerRef& target, const std::vector<std::int32_t>& message_ids, bool revoke) {
    require_session();
    if (target.kind == PeerKind::Channel) {
        fn::channels_deleteMessages req;
        req.channel = std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{target.id, target.access_hash}});
        req.id = message_ids;
        session_->call(req);
        return;
    }
    fn::messages_deleteMessages req;
    req.revoke = revoke;
    req.id = message_ids;
    session_->call(req);
}

std::vector<Message> Client::get_messages(const PeerRef& target, const std::vector<std::int32_t>& message_ids) {
    require_session();
    if (target.kind == PeerKind::Channel) {
        fn::channels_getMessages req;
        req.channel = std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{target.id, target.access_hash}});
        for (const auto id : message_ids) {
            req.id.push_back(std::make_shared<tl::InputMessage>(tl::InputMessage{ty::inputMessageID{id}}));
        }
        return collect_messages(session_->call(req));
    }
    fn::messages_getMessages req;
    for (const auto id : message_ids) {
        req.id.push_back(std::make_shared<tl::InputMessage>(tl::InputMessage{ty::inputMessageID{id}}));
    }
    return collect_messages(session_->call(req));
}

std::vector<Message> Client::get_history(const PeerRef& target, std::int32_t limit, std::int32_t offset_id) {
    require_session();
    fn::messages_getHistory req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.offset_id = offset_id;
    req.limit = limit;
    return collect_messages(session_->call(req));
}

void Client::send_chat_action(const PeerRef& target, ChatAction action) {
    require_session();
    fn::messages_setTyping req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    switch (action) {
        case ChatAction::Typing: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageTypingAction{}}); break;
        case ChatAction::Cancel: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageCancelAction{}}); break;
        case ChatAction::UploadPhoto: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageUploadPhotoAction{0}}); break;
        case ChatAction::UploadVideo: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageUploadVideoAction{0}}); break;
        case ChatAction::UploadDocument: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageUploadDocumentAction{0}}); break;
        case ChatAction::RecordVoice: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageRecordAudioAction{}}); break;
        case ChatAction::RecordVideo: req.action = std::make_shared<tl::SendMessageAction>(tl::SendMessageAction{ty::sendMessageRecordRoundAction{}}); break;
    }
    session_->call(req);
}

void Client::pin_message(const PeerRef& target, std::int32_t message_id, bool silent) {
    require_session();
    fn::messages_updatePinnedMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.silent = silent;
    req.unpin = false;
    session_->call(req);
}

void Client::unpin_message(const PeerRef& target, std::int32_t message_id) {
    require_session();
    fn::messages_updatePinnedMessage req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.id = message_id;
    req.unpin = true;
    session_->call(req);
}

std::vector<Message> Client::forward_messages(const PeerRef& from, const PeerRef& to,
                                              const std::vector<std::int32_t>& message_ids) {
    require_session();
    fn::messages_forwardMessages req;
    req.from_peer = std::make_shared<tl::InputPeer>(input_peer(from));
    req.to_peer = std::make_shared<tl::InputPeer>(input_peer(to));
    req.id = message_ids;
    for (std::size_t i = 0; i < message_ids.size(); ++i) {
        req.random_id.push_back(random_i64());
    }
    const tl::Updates updates = session_->call(req);
    std::vector<Message> out;
    if (const auto* u = std::get_if<ty::updates>(&updates.value)) {
        for (const auto& up : u->updates) {
            if (!up) continue;
            if (const auto* nm = std::get_if<ty::updateNewMessage>(&up->value)) {
                if (nm->message) if (auto msg = detail::message_from_tl(*nm->message, cache_)) out.push_back(*msg);
            } else if (const auto* ncm = std::get_if<ty::updateNewChannelMessage>(&up->value)) {
                if (ncm->message) if (auto msg = detail::message_from_tl(*ncm->message, cache_)) out.push_back(*msg);
            }
        }
    }
    return out;
}

void Client::answer_callback_query(std::int64_t query_id, const std::string& text, bool alert, const std::string& url) {
    require_session();
    fn::messages_setBotCallbackAnswer req;
    req.query_id = query_id;
    req.alert = alert;
    if (!text.empty()) req.message = text;
    if (!url.empty()) req.url = url;
    session_->call(req);
}

std::shared_ptr<tl::InputFile> Client::upload(const std::string& path, std::string& out_name, std::int64_t& out_size) {
    const Bytes data = read_file(path);
    out_name = std::filesystem::path(path).filename().string();
    out_size = static_cast<std::int64_t>(data.size());
    const std::int64_t file_id = random_i64();
    const bool big = data.size() > kSmallFileLimit;
    const std::size_t parts = (data.size() + kPartSize - 1) / kPartSize;
    if (parts == 0) {
        throw std::runtime_error("cannot upload an empty file");
    }
    for (std::size_t i = 0; i < parts; ++i) {
        const std::size_t off = i * kPartSize;
        const std::size_t len = std::min(kPartSize, data.size() - off);
        Bytes chunk(data.begin() + static_cast<std::ptrdiff_t>(off), data.begin() + static_cast<std::ptrdiff_t>(off + len));
        if (big) {
            fn::upload_saveBigFilePart req;
            req.file_id = file_id;
            req.file_part = static_cast<std::int32_t>(i);
            req.file_total_parts = static_cast<std::int32_t>(parts);
            req.bytes = std::move(chunk);
            session_->call(req);
        } else {
            fn::upload_saveFilePart req;
            req.file_id = file_id;
            req.file_part = static_cast<std::int32_t>(i);
            req.bytes = std::move(chunk);
            session_->call(req);
        }
    }
    auto file = std::make_shared<tl::InputFile>();
    if (big) {
        *file = tl::InputFile{ty::inputFileBig{file_id, static_cast<std::int32_t>(parts), out_name}};
    } else {
        *file = tl::InputFile{ty::inputFile{file_id, static_cast<std::int32_t>(parts), out_name, to_hex(mtproto::md5(data))}};
    }
    return file;
}

Message Client::send_media(const PeerRef& target, std::shared_ptr<tl::InputMedia> media, const std::string& caption) {
    require_session();
    fn::messages_sendMedia req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(target));
    req.media = std::move(media);
    req.message = caption;
    req.random_id = random_i64();
    const tl::Updates updates = session_->call(req);
    if (auto msg = find_sent_message(updates, target, caption, 0)) {
        return *msg;
    }
    throw std::runtime_error("media was sent but no message was returned by the server");
}

Message Client::send_document(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedDocument doc;
    doc.file = file;
    doc.force_file = true;
    doc.mime_type = mime_for(name);
    doc.attributes.push_back(std::make_shared<tl::DocumentAttribute>(tl::DocumentAttribute{ty::documentAttributeFilename{name}}));
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{doc}), caption);
}

Message Client::send_photo(const PeerRef& target, const std::string& path, const std::string& caption) {
    std::string name;
    std::int64_t size = 0;
    auto file = upload(path, name, size);
    ty::inputMediaUploadedPhoto photo;
    photo.file = file;
    return send_media(target, std::make_shared<tl::InputMedia>(tl::InputMedia{photo}), caption);
}

std::int64_t Client::download_media(const Message& message, const std::string& path) {
    return download_media(message, path, DownloadProgress{});
}

std::int64_t Client::download_media(const Message& message, const std::string& path, DownloadProgress progress) {
    require_session();
    std::shared_ptr<tl::InputFileLocation> location;
    if (message.media.kind == MediaKind::Photo) {
        location = std::make_shared<tl::InputFileLocation>(tl::InputFileLocation{ty::inputPhotoFileLocation{
            message.media.id, message.media.access_hash, message.media.file_reference, "y"}});
    } else if (message.media.kind == MediaKind::Document) {
        location = std::make_shared<tl::InputFileLocation>(tl::InputFileLocation{ty::inputDocumentFileLocation{
            message.media.id, message.media.access_hash, message.media.file_reference, ""}});
    } else {
        throw std::runtime_error("message has no downloadable media");
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open for writing: " + path);
    }
    std::int64_t offset = 0;
    for (;;) {
        fn::upload_getFile req;
        req.location = location;
        req.offset = offset;
        req.limit = kDownloadChunk;
        req.cdn_supported = true;
        const tl::upload_File file = session_->call(req);
        const auto* f = std::get_if<ty::upload_file>(&file.value);
        if (f == nullptr) {
            const auto* redirect = std::get_if<ty::upload_fileCdnRedirect>(&file.value);
            if (redirect == nullptr) throw std::runtime_error("upload.getFile returned an unexpected type");
            const std::int64_t total = offset + download_via_cdn(*redirect, out);
            if (progress) progress(total, message.media.size);
            return total;
        }
        out.write(reinterpret_cast<const char*>(f->bytes.data()), static_cast<std::streamsize>(f->bytes.size()));
        offset += static_cast<std::int64_t>(f->bytes.size());
        if (progress) progress(offset, message.media.size);
        if (f->bytes.size() < static_cast<std::size_t>(kDownloadChunk)) {
            break;
        }
    }
    return offset;
}

std::int64_t Client::download_via_cdn(const ty::upload_fileCdnRedirect& redirect, std::ofstream& out) {
    const DcEndpoint* endpoint = nullptr;
    for (const auto& e : dc_endpoints_) {
        if (e.id == redirect.dc_id && e.cdn) endpoint = &e;
    }
    if (endpoint == nullptr) {
        for (const auto& e : dc_endpoints_) {
            if (e.id == redirect.dc_id) endpoint = &e;
        }
    }
    if (endpoint == nullptr) {
        throw std::runtime_error("no address known for CDN DC " + std::to_string(redirect.dc_id));
    }
    debug_log(options_.debug, "cdn: connecting to DC " + std::to_string(redirect.dc_id) + " at " + endpoint->ip);
    mtproto::Connection cdn_conn(mtproto::TransportKind::Intermediate);
    cdn_conn.connect(endpoint->ip, static_cast<std::uint16_t>(endpoint->port));
    cdn_conn.set_receive_timeout(options_.connect_timeout_ms);
    PlainConnection plain(cdn_conn, options_.debug);
    mtproto::HandshakeOptions handshake;
    handshake.dc_id = redirect.dc_id;
    const mtproto::AuthKeyResult key = mtproto::create_auth_key(plain, handshake);
    mtproto::SessionData data;
    data.dc_id = redirect.dc_id;
    data.auth_key = key.auth_key;
    data.server_salt = key.server_salt;
    data.time_offset = key.time_offset;
    cdn_conn.set_receive_timeout(options_.connect_timeout_ms);
    mtproto::MtSession cdn(cdn_conn, data);

    // SHA-256 of each encrypted part, keyed by its offset. Parts are checked before they are decrypted and written.
    std::map<std::int64_t, std::pair<std::int32_t, Bytes>> hashes;
    for (const auto& h : redirect.file_hashes) {
        if (h) if (const auto* fh = std::get_if<ty::fileHash>(&h->value)) hashes[fh->offset] = {fh->limit, fh->hash};
    }
    auto verify_chunk = [&](std::int64_t start, const Bytes& enc) {
        const std::int64_t end = start + static_cast<std::int64_t>(enc.size());
        std::int64_t p = start;
        while (p < end) {
            auto it = hashes.find(p);
            if (it == hashes.end()) {
                fn::upload_getCdnFileHashes hreq;
                hreq.file_token = redirect.file_token;
                hreq.offset = p;
                const std::vector<std::shared_ptr<tl::FileHash>> fetched = session_->call(hreq);
                for (const auto& h : fetched) {
                    if (h) if (const auto* fh = std::get_if<ty::fileHash>(&h->value)) hashes[fh->offset] = {fh->limit, fh->hash};
                }
                it = hashes.find(p);
                if (it == hashes.end()) {
                    throw std::runtime_error("no CDN hash for file part at offset " + std::to_string(p));
                }
            }
            const std::int64_t len = it->second.first;
            if (len <= 0 || p + len > end) {
                throw std::runtime_error("CDN hash part at offset " + std::to_string(p) + " does not line up with the chunk");
            }
            const Bytes part(enc.begin() + (p - start), enc.begin() + (p - start + len));
            if (mtproto::sha256(part) != it->second.second) {
                throw std::runtime_error("CDN part at offset " + std::to_string(p) + " failed its SHA-256 check");
            }
            p += len;
        }
    };

    std::int64_t offset = 0;
    for (;;) {
        fn::upload_getCdnFile req;
        req.file_token = redirect.file_token;
        req.offset = offset;
        req.limit = kDownloadChunk;
        const tl::upload_CdnFile chunk = cdn.call(req);
        const auto* cf = std::get_if<ty::upload_cdnFile>(&chunk.value);
        if (cf == nullptr) {
            throw std::runtime_error("CDN asked for a reupload; reupload is not supported yet");
        }
        if (options_.verify_cdn_hashes) verify_chunk(offset, cf->bytes);
        const Bytes plain_bytes = mtproto::aes_ctr_crypt(cf->bytes, redirect.encryption_key,
                                                         mtproto::cdn_counter_iv(redirect.encryption_iv, offset));
        out.write(reinterpret_cast<const char*>(plain_bytes.data()), static_cast<std::streamsize>(plain_bytes.size()));
        offset += static_cast<std::int64_t>(plain_bytes.size());
        if (plain_bytes.size() < static_cast<std::size_t>(kDownloadChunk)) break;
    }
    cdn_conn.close();
    return offset;
}

void Client::join_chat_by_invite(const std::string& invite_hash) {
    require_session();
    fn::messages_importChatInvite req;
    req.hash = invite_hash;
    session_->call(req);
}

void Client::join_channel(const PeerRef& channel) {
    require_session();
    fn::channels_joinChannel req;
    req.channel = std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{channel.id, channel.access_hash}});
    session_->call(req);
}

void Client::leave_channel(const PeerRef& channel) {
    require_session();
    fn::channels_leaveChannel req;
    req.channel = std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{channel.id, channel.access_hash}});
    session_->call(req);
}

}  // namespace cppgram
