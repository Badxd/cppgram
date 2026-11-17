#pragma once

#include <memory>

#include "cppgram/metrics.h"

#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/export.h"

#include <curl/curl.h>

#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>

namespace cppgram::bot {

/**
 * @brief This class makes http requests via libcurl.
 *
 * @ingroup net
 */
class CPPGRAM_API CurlHttpClient : public HttpClient {
public:
    CurlHttpClient();
    ~CurlHttpClient() override;

    /**
     * @brief Sends a request to the url.
     *
     * If there are no fields, a GET request is sent. Otherwise, a multipart/form-data POST request is sent.
     */
    std::string makeRequest(const std::string& url, std::span<const HttpFormField> fields) const override;

    /**
     * @brief Raw curl handles, each thread has its own handle.
     */
    std::unordered_map<std::thread::id, CURL*> curlHandles;

    /**
     * @brief Lock for curlHandles access.
     */
    std::mutex curlHandlesMutex;

    /**
     * @brief Proxy URL (NULL = no proxy).
     */
    void setProxy(const char* url = NULL, long timeout = 20L) {
        _proxyUrl = url;
        _connectTimeout = timeout;
    }

    // Optional transport metrics: cppgram_bot_http_responses_total{status} and cppgram_bot_http_timeouts_total.
    void set_metrics(const std::shared_ptr<metrics::Registry>& registry);

private:
    const char* _proxyUrl = NULL;
    metrics::CounterFamily _http_responses;
    metrics::Counter _timeouts;
    long _connectTimeout = 20L;
};

} // namespace cppgram::bot
