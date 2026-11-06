#include "cppgram/bot/Api.h"

#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/TgException.h"
#include "cppgram/log.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <utility>

namespace cppgram::bot {

namespace {
double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

Api::Api(std::string token, const HttpClient& httpClient, std::string url)
    : _httpClient(httpClient)
    , _token(std::move(token))
    , _url(std::move(url)) {
}

std::string Api::downloadFile(std::string_view filePath, const std::vector<HttpFormField>& fields) const {
    std::string url;
    url.reserve(_url.size() + _token.size() + filePath.size() + 10);
    url.append(_url).append("/file/bot").append(_token).append("/").append(filePath);
    return _httpClient.makeRequest(url, fields);
}

void Api::set_metrics(const std::shared_ptr<metrics::Registry>& registry) {
    if (!registry) {
        _requests = {};
        _api_errors = {};
        _duration = {};
        return;
    }
    static const std::vector<double> kBotLatency{0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10, 30};
    _requests = registry->counter("cppgram_bot_requests_total", "Bot API requests sent, by method.", {"method"});
    _api_errors = registry->counter("cppgram_bot_api_errors_total",
                                    "Bot API failures, by error code. Use transport or invalid_response for failures without one.",
                                    {"code"});
    _duration = registry->histogram("cppgram_bot_request_duration_seconds",
                                    "Bot API HTTP round-trip time, by method.", kBotLatency, {"method"});
}

nlohmann::json Api::sendRequest(std::string_view method, const std::vector<HttpFormField>& fields) const {
    // Only the method name is logged. The token sits in the request URL and never reaches a log line.
    std::string url;
    url.reserve(_url.size() + _token.size() + method.size() + 6);
    url.append(_url).append("/bot").append(_token).append("/").append(method);
    const std::string method_name(method);
    const auto started = std::chrono::steady_clock::now();
    _requests.labels({method_name}).inc();
    std::string body;
    try {
        body = _httpClient.makeRequest(url, fields);
    } catch (...) {
        _api_errors.labels({"transport"}).inc();
        _duration.labels({method_name}).observe(seconds_since(started));
        throw;
    }
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    _duration.labels({method_name}).observe(seconds_since(started));
    if (body.starts_with("<html>")) {
        _api_errors.labels({"invalid_response"}).inc();
        log::write(log::Level::Warn, "bot_api", "html response instead of api response", {{"method", method_name}});
        throw TgException("cppgram received HTML instead of a Telegram Bot API response",
                          TgException::ErrorCode::HtmlResponse);
    }

    nlohmann::json response;
    try {
        response = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& exception) {
        _api_errors.labels({"invalid_response"}).inc();
        log::write(log::Level::Warn, "bot_api", "response is not valid json", {{"method", method_name}});
        throw TgException(exception.what(), TgException::ErrorCode::InvalidJson);
    }
    if (!response.value("ok", false)) {
        std::shared_ptr<ResponseParameters> parameters;
        if (const auto iterator = response.find("parameters"); iterator != response.end()) {
            parameters = std::make_shared<ResponseParameters>(iterator->get<ResponseParameters>());
        }
        const std::string description = response.value("description", "Telegram Bot API request failed");
        const int error_code = response.value("error_code", 0);
        _api_errors.labels({std::to_string(error_code)}).inc();
        log::write(log::Level::Warn, "bot_api", "request failed",
                   {{"method", method_name}, {"error_code", std::to_string(error_code)}, {"description", description}});
        throw TgException(description, static_cast<TgException::ErrorCode>(error_code), std::move(parameters));
    }

    log::write(log::Level::Debug, "bot_api", "request ok",
               {{"method", method_name}, {"elapsed_ms", std::to_string(elapsed_ms)}});
    return response.at("result");
}

} // namespace cppgram::bot
