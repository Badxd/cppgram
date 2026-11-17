// Outbound checks for inline keyboards built by the caller. Header-only and opt-in: the serializers do not call these,
// so existing code that sends keyboards keeps its current behavior.
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "cppgram/bot/Types.h"

namespace cppgram::bot {

/**
 * @brief Checks one InlineKeyboardButton before it is sent.
 *
 * A button must set exactly one action field: url, callback_data, web_app, login_url, switch_inline_query,
 * switch_inline_query_current_chat, switch_inline_query_chosen_chat, copy_text, callback_game or pay. The disabled
 * field is not an action and is ignored here. callback_data must be 1 to 64 bytes.
 *
 * @return std::nullopt when valid, otherwise a description of the first problem.
 */
inline std::optional<std::string> validateInlineKeyboardButton(const InlineKeyboardButton& button) {
    std::size_t actions = 0;
    actions += button.url.has_value();
    actions += button.callbackData.has_value();
    actions += button.webApp != nullptr;
    actions += button.loginUrl != nullptr;
    actions += button.switchInlineQuery.has_value();
    actions += button.switchInlineQueryCurrentChat.has_value();
    actions += button.switchInlineQueryChosenChat != nullptr;
    actions += button.copyText != nullptr;
    actions += button.callbackGame != nullptr;
    actions += button.pay.has_value();

    if (actions != 1) {
        return "inline keyboard button must set exactly one action field, found " + std::to_string(actions);
    }

    if (button.callbackData && (button.callbackData->empty() || button.callbackData->size() > 64)) {
        return "inline keyboard button callback_data must be 1 to 64 bytes, found "
            + std::to_string(button.callbackData->size());
    }

    return std::nullopt;
}

/**
 * @brief Checks an InlineKeyboardMarkup before it is sent.
 *
 * An empty keyboard is valid: it is how a keyboard is removed. Every button must be non-null and pass
 * validateInlineKeyboardButton. Empty rows are accepted here; Telegram's handling of them is not checked.
 *
 * @return std::nullopt when valid, otherwise the position (row, column) and the problem.
 */
inline std::optional<std::string> validateInlineKeyboardMarkup(const InlineKeyboardMarkup& markup) {
    for (std::size_t row = 0; row < markup.inlineKeyboard.size(); ++row) {
        const auto& buttons = markup.inlineKeyboard[row];
        for (std::size_t column = 0; column < buttons.size(); ++column) {
            const auto& button = buttons[column];
            const std::string where = "row " + std::to_string(row) + ", column " + std::to_string(column) + ": ";
            if (!button) {
                return where + "button is null";
            }
            if (auto error = validateInlineKeyboardButton(*button)) {
                return where + *error;
            }
        }
    }
    return std::nullopt;
}

} // namespace cppgram::bot
