#include "cppgram/bot/TgException.h"

#include <utility>

namespace cppgram::bot {

TgException::TgException(std::string_view description, ErrorCode errorCode,
                         std::shared_ptr<ResponseParameters> parameters)
    : runtime_error(std::string(description))
    , errorCode(errorCode)
    , parameters(std::move(parameters)) {
}

} // namespace cppgram::bot
