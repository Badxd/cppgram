#include "cppgram/bot/HttpClient.h"

namespace cppgram::bot {

RequestCancelled::RequestCancelled()
    : std::runtime_error("request cancelled") {
}

RequestCancelled::RequestCancelled(std::string_view request)
    : std::runtime_error(std::string("request cancelled: ").append(request)) {
}

} // namespace cppgram::bot
