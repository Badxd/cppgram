#pragma once

#include "cppgram/metrics.h"

#include "cppgram/bot/HttpFormField.h"
#include "cppgram/bot/InputFile.h"
#include "cppgram/bot/Types.h"
#include "cppgram/bot/export.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cppgram::bot {

class Bot;
class HttpClient;

/**
 * @brief Executes methods from the Telegram Bot API.
 *
 * Method declarations and their documentation are generated from the official
 * Telegram Bot API documentation.
 *
 * @ingroup api
 */
class CPPGRAM_API Api {
    friend class Bot;

public:
    Api(std::string token, const HttpClient& httpClient, std::string url);

    // Optional Bot API metrics: cppgram_bot_requests_total{method}, cppgram_bot_api_errors_total{code},
    // cppgram_bot_request_duration_seconds{method}. Pass nullptr to turn them off.
    void set_metrics(const std::shared_ptr<metrics::Registry>& registry);

#include "cppgram/bot/ApiMethods.inc.h"

    /**
     * @brief Downloads a Telegram file and returns its contents.
     */
    std::string downloadFile(std::string_view filePath, const std::vector<HttpFormField>& fields = { }) const;

    const HttpClient& _httpClient;
    metrics::CounterFamily _requests;
    metrics::CounterFamily _api_errors;
    metrics::HistogramFamily _duration;

protected:
    nlohmann::json sendRequest(std::string_view method, const std::vector<HttpFormField>& fields) const;

    const std::string _token;
    const std::string _url;
};

} // namespace cppgram::bot
