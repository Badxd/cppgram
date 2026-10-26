#include "cppgram/client.h"

#include <stdexcept>

#include "cppgram/convert.h"

namespace cppgram {

namespace fn = mtproto::tl::functions;
namespace ty = mtproto::tl::types;
namespace tl = mtproto::tl;

namespace {

std::shared_ptr<tl::InputBusinessRecipients> all_recipients() {
    ty::inputBusinessRecipients r{};
    r.existing_chats = true;
    r.new_chats = true;
    r.contacts = true;
    r.non_contacts = true;
    r.exclude_selected = false;
    return std::make_shared<tl::InputBusinessRecipients>(tl::InputBusinessRecipients{r});
}

}  // namespace

// ---- phase 13: business away and greeting messages

void Client::set_business_away_message(std::int32_t shortcut_id, bool offline_only) {
    require_session();
    ty::inputBusinessAwayMessage m{};
    m.offline_only = offline_only;
    m.shortcut_id = shortcut_id;
    m.schedule = std::make_shared<tl::BusinessAwayMessageSchedule>(
        tl::BusinessAwayMessageSchedule{ty::businessAwayMessageScheduleAlways{}});
    m.recipients = all_recipients();
    fn::account_updateBusinessAwayMessage req;
    req.message = std::make_shared<tl::InputBusinessAwayMessage>(tl::InputBusinessAwayMessage{m});
    session_->call(req);
}

void Client::clear_business_away_message() {
    require_session();
    fn::account_updateBusinessAwayMessage req;
    req.message = std::nullopt;
    session_->call(req);
}

void Client::set_business_greeting_message(std::int32_t shortcut_id, std::int32_t no_activity_days) {
    require_session();
    ty::inputBusinessGreetingMessage m{};
    m.shortcut_id = shortcut_id;
    m.recipients = all_recipients();
    m.no_activity_days = no_activity_days;
    fn::account_updateBusinessGreetingMessage req;
    req.message = std::make_shared<tl::InputBusinessGreetingMessage>(tl::InputBusinessGreetingMessage{m});
    session_->call(req);
}

void Client::clear_business_greeting_message() {
    require_session();
    fn::account_updateBusinessGreetingMessage req;
    req.message = std::nullopt;
    session_->call(req);
}

// ---- phase 14: stealth mode and sticker set edits

void Client::enable_stealth_mode(bool past, bool future) {
    require_session();
    fn::stories_activateStealthMode req;
    req.past = past;
    req.future = future;
    session_->call(req);
}

namespace {

std::shared_ptr<tl::InputDocument> input_doc_of(const std::vector<MediaInfo>& stickers, std::int64_t document_id) {
    for (const auto& s : stickers) {
        if (s.id != document_id) continue;
        ty::inputDocument d{};
        d.id = s.id;
        d.access_hash = s.access_hash;
        d.file_reference = s.file_reference;
        return std::make_shared<tl::InputDocument>(tl::InputDocument{d});
    }
    throw std::runtime_error("sticker " + std::to_string(document_id) + " is not in this set");
}

}  // namespace

void Client::change_sticker_position(const std::string& short_name, std::int64_t document_id, std::int32_t position) {
    require_session();
    const StickerSetContent set = get_sticker_set(short_name);
    fn::stickers_changeStickerPosition req;
    req.sticker = input_doc_of(set.stickers, document_id);
    req.position = position;
    session_->call(req);
}

void Client::remove_sticker_from_set(const std::string& short_name, std::int64_t document_id) {
    require_session();
    const StickerSetContent set = get_sticker_set(short_name);
    fn::stickers_removeStickerFromSet req;
    req.sticker = input_doc_of(set.stickers, document_id);
    session_->call(req);
}

}  // namespace cppgram
