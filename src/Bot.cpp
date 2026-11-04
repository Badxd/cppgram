#include "cppgram/bot/Bot.h"
#include "cppgram/bot/CurlHttpClient.h"
#include "cppgram/bot/EventBroadcaster.h"

#include <memory>
#include <utility>

namespace cppgram::bot {

Bot::Bot(std::string token, const HttpClient& httpClient, std::string url)
    : _token(std::move(token))
    , _api(_token, httpClient, std::move(url))
    , _eventBroadcaster(std::make_unique<EventBroadcaster>())
    , _eventHandler(getEvents()) {
}

HttpClient& Bot::_getDefaultHttpClient() {
    static CurlHttpClient instance;
    return instance;
}

} // namespace cppgram::bot
