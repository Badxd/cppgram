// Internal helpers for keyboard and markup conversion. Not installed.
#pragma once

#include <memory>
#include <optional>

#include "cppgram/client.h"
#include "cppgram/mtproto/schema/api.h"

namespace cppgram::detail {

// Outbound: public types to TL. Invalid combinations throw std::invalid_argument before any RPC.
std::shared_ptr<mtproto::tl::KeyboardInlineButton> to_tl_inline_button(const InlineButton& b);
std::shared_ptr<mtproto::tl::ReplyMarkup> to_tl_inline_markup(const InlineKeyboard& kb);
std::shared_ptr<mtproto::tl::ReplyMarkup> to_tl_reply_markup(const ReplyKeyboard& kb);

// Inbound: TL to public types. A button or markup variant that the public types cannot represent throws
// errors::SerializationError. Nothing is dropped silently.
InlineButton from_tl_inline_button(const mtproto::tl::KeyboardInlineButton& b);
ReplyKeyboardButton from_tl_reply_button(const mtproto::tl::KeyboardButton& b);
// Inline keyboard of an inline markup. nullopt for every other markup variant.
std::optional<InlineKeyboard> from_tl_inline_markup(const mtproto::tl::ReplyMarkup& markup);
// Reply keyboard of a reply markup (keyboard, force reply or hide). nullopt for an inline markup.
std::optional<ReplyKeyboard> from_tl_reply_markup(const mtproto::tl::ReplyMarkup& markup);

// Input media for a file uploaded with save_file. as_photo selects a photo, otherwise a document with its MIME type
// and file name attribute. Defined in send.cpp, shared with send_uploaded.
std::shared_ptr<mtproto::tl::InputMedia> uploaded_input_media(const UploadedFile& file, bool as_photo);

}  // namespace cppgram::detail
