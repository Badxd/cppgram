#include "cppgram/client.h"

#include <memory>
#include <optional>
#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputChannel> channel_ptr(const PeerRef& p) {
    if (p.kind != PeerKind::Channel) throw std::invalid_argument("expected a channel or supergroup");
    if (!p.has_access_hash) throw std::runtime_error("channel access hash unknown; resolve the channel first");
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{p.id, p.access_hash}});
}

}  // namespace

std::int32_t Client::get_account_ttl() {
    require_session();
    fn::account_getAccountTTL req;
    const tl::AccountDaysTTL res = session_->call(req);
    const auto* d = std::get_if<ty::accountDaysTTL>(&res.value);
    if (d == nullptr) throw std::runtime_error("get_account_ttl: unexpected response");
    return d->days;
}

bool Client::set_account_ttl(std::int32_t days) {
    require_session();
    if (days < 1) throw std::invalid_argument("set_account_ttl: days must be at least 1");
    fn::account_setAccountTTL req;
    req.ttl = std::make_shared<tl::AccountDaysTTL>(tl::AccountDaysTTL{ty::accountDaysTTL{days}});
    return session_->call(req);
}

void Client::set_chat_ttl(const PeerRef& chat, std::int32_t period_seconds) {
    require_session();
    if (period_seconds < 0) throw std::invalid_argument("set_chat_ttl: period must not be negative");
    fn::messages_setHistoryTTL req;
    req.peer = std::make_shared<tl::InputPeer>(input_peer(chat));
    req.period = period_seconds;
    session_->call(req);
}

bool Client::set_chat_username(const PeerRef& channel, const std::string& username) {
    require_session();
    fn::channels_updateUsername req;
    req.channel = channel_ptr(channel);
    req.username = username;
    return session_->call(req);
}

bool Client::set_chat_discussion_group(const PeerRef& broadcast, const PeerRef& group) {
    require_session();
    fn::channels_setDiscussionGroup req;
    req.broadcast = channel_ptr(broadcast);
    req.group = channel_ptr(group);
    return session_->call(req);
}

void Client::toggle_join_to_send(const PeerRef& channel, bool enabled) {
    require_session();
    fn::channels_toggleJoinToSend req;
    req.channel = channel_ptr(channel);
    req.enabled = enabled;
    session_->call(req);
}

void Client::toggle_forum_topics(const PeerRef& channel, bool enabled, bool tabs) {
    require_session();
    fn::channels_toggleForum req;
    req.channel = channel_ptr(channel);
    req.enabled = enabled;
    req.tabs = tabs;
    session_->call(req);
}

void Client::set_chat_accent_color(const PeerRef& channel, std::int32_t color) {
    require_session();
    if (color < 0) throw std::invalid_argument("set_chat_accent_color: color must not be negative");
    fn::channels_updateColor req;
    req.for_profile = false;
    req.channel = channel_ptr(channel);
    req.color = color;
    session_->call(req);
}

void Client::set_chat_profile_accent_color(const PeerRef& channel, std::int32_t color) {
    require_session();
    if (color < 0) throw std::invalid_argument("set_chat_profile_accent_color: color must not be negative");
    fn::channels_updateColor req;
    req.for_profile = true;
    req.channel = channel_ptr(channel);
    req.color = color;
    session_->call(req);
}

bool Client::toggle_folder_tags(bool enabled) {
    require_session();
    fn::messages_toggleDialogFilterTags req;
    req.enabled = enabled;
    return session_->call(req);
}

}  // namespace cppgram
