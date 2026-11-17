#pragma once

#include "cppgram/bot/Types.h"
#include "cppgram/bot/export.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace cppgram::bot {

/**
 * @brief Exception type which is only thrown when Telegram refuses API request.
 *
 * @ingroup general
 */
class CPPGRAM_API TgException : public std::runtime_error {
public:
    /**
     * @brief Enum of possible errors from Api requests 
     */
    enum class ErrorCode : size_t {
        Undefined = 0,
        BadRequest = 400,
        Unauthorized = 401,
        Forbidden = 403,
        NotFound = 404,
        Flood = 429,
        Internal = 500,
        HtmlResponse = 100,
        InvalidJson = 101
    };

    TgException(std::string_view description, ErrorCode errorCode,
                std::shared_ptr<ResponseParameters> parameters = nullptr);

    const ErrorCode errorCode;

    /**
     * @brief Additional details about the error returned by Telegram, if any.
     */
    const std::shared_ptr<ResponseParameters> parameters;
};

} // namespace cppgram::bot
