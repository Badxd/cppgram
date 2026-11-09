// account.* and auth.* calls for settings, verification, takeout and profile fields.
#include "cppgram/client.h"

#include <stdexcept>
#include <variant>

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::ProfileTab> profile_tab_ptr_for(ProfileTab tab) {
    switch (tab) {
        case ProfileTab::Posts: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabPosts{}});
        case ProfileTab::Gifts: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabGifts{}});
        case ProfileTab::Media: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabMedia{}});
        case ProfileTab::Files: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabFiles{}});
        case ProfileTab::Music: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabMusic{}});
        case ProfileTab::Voice: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabVoice{}});
        case ProfileTab::Links: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabLinks{}});
        case ProfileTab::Gifs: return std::make_shared<tl::ProfileTab>(tl::ProfileTab{ty::profileTabGifs{}});
    }
    throw std::invalid_argument("unknown profile tab");
}

LoginCodeInfo login_code_info_of(const tl::auth_SentCode& result) {
    const auto* sent = std::get_if<ty::auth_sentCode>(&result.value);
    if (sent == nullptr) throw std::runtime_error("unexpected auth.SentCode variant");
    LoginCodeInfo info;
    info.phone_code_hash = sent->phone_code_hash;
    if (sent->timeout) info.timeout_seconds = *sent->timeout;
    info.delivery = "other";
    if (sent->type) {
        const auto& t = sent->type->value;
        if (std::holds_alternative<ty::auth_sentCodeTypeApp>(t)) info.delivery = "app";
        else if (std::holds_alternative<ty::auth_sentCodeTypeSms>(t)) info.delivery = "sms";
        else if (std::holds_alternative<ty::auth_sentCodeTypeCall>(t)) info.delivery = "call";
        else if (std::holds_alternative<ty::auth_sentCodeTypeFlashCall>(t)) info.delivery = "flash_call";
        else if (std::holds_alternative<ty::auth_sentCodeTypeEmailCode>(t)) info.delivery = "email";
    }
    return info;
}

std::shared_ptr<tl::EmailVerifyPurpose> purpose_ptr(const EmailVerifyPurposeInfo& p) {
    switch (p.kind) {
        case EmailVerifyPurposeInfo::Kind::LoginSetup: {
            if (p.phone_number.empty() || p.phone_code_hash.empty()) {
                throw std::invalid_argument("LoginSetup email purpose needs phone_number and phone_code_hash");
            }
            ty::emailVerifyPurposeLoginSetup v;
            v.phone_number = p.phone_number;
            v.phone_code_hash = p.phone_code_hash;
            return std::make_shared<tl::EmailVerifyPurpose>(tl::EmailVerifyPurpose{v});
        }
        case EmailVerifyPurposeInfo::Kind::LoginChange:
            return std::make_shared<tl::EmailVerifyPurpose>(tl::EmailVerifyPurpose{ty::emailVerifyPurposeLoginChange{}});
        case EmailVerifyPurposeInfo::Kind::Passport:
            return std::make_shared<tl::EmailVerifyPurpose>(tl::EmailVerifyPurpose{ty::emailVerifyPurposePassport{}});
    }
    throw std::invalid_argument("unknown email verify purpose");
}

std::shared_ptr<tl::InputChannel> input_channel_of(const PeerRef& channel) {
    if (channel.kind != PeerKind::Channel || !channel.has_access_hash) {
        throw std::invalid_argument("expected a channel peer with an access hash");
    }
    return std::make_shared<tl::InputChannel>(tl::InputChannel{ty::inputChannel{channel.id, channel.access_hash}});
}

std::shared_ptr<tl::InputDocument> input_document_from(const DocumentRef& d) {
    return std::make_shared<tl::InputDocument>(tl::InputDocument{ty::inputDocument{d.id, d.access_hash, d.file_reference}});
}

std::shared_ptr<tl::CodeSettings> default_code_settings() {
    return std::make_shared<tl::CodeSettings>(tl::CodeSettings{ty::codeSettings{}});
}

}  // namespace

std::int64_t Client::change_phone(const std::string& new_phone, const std::string& phone_code_hash,
                                  const std::string& code) {
    require_session();
    fn::account_changePhone req;
    req.phone_number = new_phone;
    req.phone_code_hash = phone_code_hash;
    req.phone_code = code;
    const tl::User user = call_rpc(req);
    const auto* u = std::get_if<ty::user>(&user.value);
    if (u == nullptr) throw std::runtime_error("account.changePhone returned no user");
    return u->id;
}

LoginCodeInfo Client::send_change_phone_code(const std::string& new_phone) {
    require_session();
    if (new_phone.empty()) throw std::invalid_argument("send_change_phone_code needs a phone number");
    fn::account_sendChangePhoneCode req;
    req.phone_number = new_phone;
    req.settings = default_code_settings();
    return login_code_info_of(call_rpc(req));
}

LoginCodeInfo Client::send_verify_phone_code(const std::string& phone) {
    require_session();
    if (phone.empty()) throw std::invalid_argument("send_verify_phone_code needs a phone number");
    fn::account_sendVerifyPhoneCode req;
    req.phone_number = phone;
    req.settings = default_code_settings();
    return login_code_info_of(call_rpc(req));
}

LoginCodeInfo Client::check_paid_auth(const std::string& phone, const std::string& phone_code_hash,
                                      std::int64_t form_id) {
    require_session();
    fn::auth_checkPaidAuth req;
    req.phone_number = phone;
    req.phone_code_hash = phone_code_hash;
    req.form_id = form_id;
    return login_code_info_of(call_rpc(req));
}

SentEmailCodeInfo Client::send_verify_email_code(const EmailVerifyPurposeInfo& purpose, const std::string& email) {
    require_session();
    if (email.empty()) throw std::invalid_argument("send_verify_email_code needs an email");
    fn::account_sendVerifyEmailCode req;
    req.purpose = purpose_ptr(purpose);
    req.email = email;
    const tl::account_SentEmailCode result = call_rpc(req);
    const auto* s = std::get_if<ty::account_sentEmailCode>(&result.value);
    if (s == nullptr) throw std::runtime_error("unexpected account.SentEmailCode variant");
    return SentEmailCodeInfo{s->email_pattern, s->length};
}

std::string Client::verify_email(const EmailVerifyPurposeInfo& purpose, const EmailVerificationInfo& verification) {
    require_session();
    fn::account_verifyEmail req;
    req.purpose = purpose_ptr(purpose);
    switch (verification.kind) {
        case EmailVerificationInfo::Kind::Code:
            req.verification = std::make_shared<tl::EmailVerification>(tl::EmailVerification{ty::emailVerificationCode{verification.value}});
            break;
        case EmailVerificationInfo::Kind::Google:
            req.verification = std::make_shared<tl::EmailVerification>(tl::EmailVerification{ty::emailVerificationGoogle{verification.value}});
            break;
        case EmailVerificationInfo::Kind::Apple:
            req.verification = std::make_shared<tl::EmailVerification>(tl::EmailVerification{ty::emailVerificationApple{verification.value}});
            break;
    }
    const tl::account_EmailVerified result = call_rpc(req);
    const auto* v = std::get_if<ty::account_emailVerified>(&result.value);
    if (v == nullptr) throw std::runtime_error("unexpected account.EmailVerified variant");
    return v->email;
}

bool Client::set_authorization_ttl(std::int32_t days) {
    require_session();
    fn::account_setAuthorizationTTL req;
    req.authorization_ttl_days = days;
    return call_rpc(req);
}

bool Client::set_main_profile_tab(ProfileTab tab) {
    require_session();
    fn::account_setMainProfileTab req;
    req.tab = profile_tab_ptr_for(tab);
    return call_rpc(req);
}

bool Client::update_birthday(const std::optional<BirthdayInfo>& birthday) {
    require_session();
    fn::account_updateBirthday req;
    if (birthday) {
        if (birthday->day < 1 || birthday->day > 31 || birthday->month < 1 || birthday->month > 12) {
            throw std::invalid_argument("birthday day must be 1 to 31 and month 1 to 12");
        }
        ty::birthday b;
        b.day = birthday->day;
        b.month = birthday->month;
        if (birthday->year) b.year = *birthday->year;
        req.birthday = std::make_shared<tl::Birthday>(tl::Birthday{b});
    }
    return call_rpc(req);
}

bool Client::update_color(bool for_profile, const PeerColorInfo& color) {
    require_session();
    fn::account_updateColor req;
    if (for_profile) req.for_profile = true;
    if (color.color || color.background_emoji_id) {
        ty::peerColor c;
        if (color.color) c.color = *color.color;
        if (color.background_emoji_id) c.background_emoji_id = *color.background_emoji_id;
        req.color = std::make_shared<tl::PeerColor>(tl::PeerColor{c});
    }
    return call_rpc(req);
}

bool Client::update_emoji_status(const EmojiStatusInfo& status) {
    require_session();
    fn::account_updateEmojiStatus req;
    if (status.kind == EmojiStatusInfo::Kind::Empty) {
        req.emoji_status = std::make_shared<tl::EmojiStatus>(tl::EmojiStatus{ty::emojiStatusEmpty{}});
    } else {
        ty::emojiStatus s;
        s.document_id = status.document_id;
        if (status.until) s.until = *status.until;
        req.emoji_status = std::make_shared<tl::EmojiStatus>(tl::EmojiStatus{s});
    }
    return call_rpc(req);
}

bool Client::update_notify_settings(const PeerRef& peer, std::int32_t top_msg_id, const NotifySettingsInfo& settings) {
    require_session();
    fn::account_updateNotifySettings req;
    if (top_msg_id != 0) {
        ty::inputNotifyForumTopic n;
        n.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
        n.top_msg_id = top_msg_id;
        req.peer = std::make_shared<tl::InputNotifyPeer>(tl::InputNotifyPeer{n});
    } else {
        ty::inputNotifyPeer n;
        n.peer = std::make_shared<tl::InputPeer>(input_peer(peer));
        req.peer = std::make_shared<tl::InputNotifyPeer>(tl::InputNotifyPeer{n});
    }
    ty::inputPeerNotifySettings s;
    if (settings.show_previews) s.show_previews = *settings.show_previews;
    if (settings.silent) s.silent = *settings.silent;
    if (settings.mute_until) s.mute_until = *settings.mute_until;
    if (settings.stories_muted) s.stories_muted = *settings.stories_muted;
    if (settings.stories_hide_sender) s.stories_hide_sender = *settings.stories_hide_sender;
    req.settings = std::make_shared<tl::InputPeerNotifySettings>(tl::InputPeerNotifySettings{s});
    return call_rpc(req);
}

bool Client::update_personal_channel(const PeerRef& channel) {
    require_session();
    fn::account_updatePersonalChannel req;
    req.channel = input_channel_of(channel);
    return call_rpc(req);
}

bool Client::save_music(const DocumentRef& document, bool unsave, const std::optional<DocumentRef>& after) {
    require_session();
    fn::account_saveMusic req;
    req.id = input_document_from(document);
    if (unsave) req.unsave = true;
    if (after) req.after_id = input_document_from(*after);
    return call_rpc(req);
}

std::int64_t Client::init_takeout_session(const TakeoutOptions& options) {
    require_session();
    fn::account_initTakeoutSession req;
    if (options.contacts) req.contacts = true;
    if (options.message_users) req.message_users = true;
    if (options.message_chats) req.message_chats = true;
    if (options.message_megagroups) req.message_megagroups = true;
    if (options.message_channels) req.message_channels = true;
    if (options.files) req.files = true;
    if (options.file_max_size) req.file_max_size = *options.file_max_size;
    const tl::account_Takeout result = call_rpc(req);
    const auto* t = std::get_if<ty::account_takeout>(&result.value);
    if (t == nullptr) throw std::runtime_error("unexpected account.Takeout variant");
    return t->id;
}

bool Client::finish_takeout_session(bool success) {
    require_session();
    fn::account_finishTakeoutSession req;
    if (success) req.success = true;
    return call_rpc(req);
}

bool Client::reset_authorizations() {
    require_session();
    fn::auth_resetAuthorizations req;
    return call_rpc(req);
}

}  // namespace cppgram
