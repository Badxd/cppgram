#include "cppgram/bot/CurlHttpClient.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <variant>

#include "cppgram/errors.h"
#include "cppgram/log.h"

namespace cppgram::bot {

namespace {

struct RequestCancelState {
    RequestCancelState(const std::atomic<bool>* const isEternalCancel,
                       const std::atomic<uint64_t>* const globalCancelEpoch, const uint64_t currentCancelEpoch)
        : isEternalCancel(isEternalCancel)
        , globalCancelEpoch(globalCancelEpoch)
        , currentCancelEpoch(currentCancelEpoch) {
    }

    const std::atomic<bool>* const isEternalCancel = nullptr;
    const std::atomic<uint64_t>* const globalCancelEpoch = nullptr;
    const uint64_t currentCancelEpoch = 0;
};
} // namespace

CurlHttpClient::CurlHttpClient() {
}

CurlHttpClient::~CurlHttpClient() {
    std::lock_guard<std::mutex> lock(curlHandlesMutex);
    for (auto& c : curlHandles) {
        curl_easy_cleanup(c.second);
    }
}

static CURL* getCurlHandle(const CurlHttpClient* c_) {
    CurlHttpClient* c = const_cast<CurlHttpClient*>(c_);

    std::lock_guard<std::mutex> lock(c->curlHandlesMutex);
    auto id = std::this_thread::get_id();
    auto it = c->curlHandles.find(id);
    if (it == c->curlHandles.end()) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("curl_easy_init() failed");
        }
        c->curlHandles[id] = curl;
        return curl;
    }

    return it->second;
}

static int curlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                                curl_off_t ulnow) {
    const RequestCancelState* const state = static_cast<const RequestCancelState* const>(clientp);
    if (state
        && ((state->isEternalCancel && state->isEternalCancel->load())
            || (state->globalCancelEpoch && state->currentCancelEpoch < state->globalCancelEpoch->load()))) {
        return 1;
    }

    return 0;
}

static std::size_t curlWriteString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

void CurlHttpClient::set_metrics(const std::shared_ptr<metrics::Registry>& registry) {
    if (!registry) {
        _http_responses = {};
        _timeouts = {};
        return;
    }
    _http_responses = registry->counter("cppgram_bot_http_responses_total",
                                        "HTTP responses from the Bot API host, by status (0 when no response arrived).",
                                        {"status"});
    _timeouts = registry->counter("cppgram_bot_http_timeouts_total", "Bot API HTTP requests that timed out.").labels({});
}

namespace {
// libcurl can echo the request URL in its messages, and the Bot API URL carries the bot token. The full request URL
// and the proxy URL (which can hold credentials) are replaced before any error text leaves this file.
std::string scrub_text(std::string text, std::string_view secret) {
    if (secret.empty()) return text;
    std::size_t pos = 0;
    while ((pos = text.find(secret, pos)) != std::string::npos) {
        text.replace(pos, secret.size(), "<redacted>");
        pos += 10;
    }
    return text;
}

std::string method_of(const std::string& url) {
    const std::size_t slash = url.rfind('/');
    return slash == std::string::npos ? url : url.substr(slash + 1);
}
} // namespace

std::string CurlHttpClient::makeRequest(const std::string& url, std::span<const HttpFormField> fields) const {
    CURL* curl = getCurlHandle(this);

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, _connectTimeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, _timeout);
    curl_easy_setopt(curl, CURLOPT_PROXY, _proxyUrl);

    const RequestCancelState state { &_isEternalCancel, &_cancelEpoch, _cancelEpoch.load() };

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    curl_mime* mime;
    curl_mimepart* part;
    mime = curl_mime_init(curl);
    if (!fields.empty()) {
        for (const HttpFormField& field : fields) {
            part = curl_mime_addpart(mime);

            curl_mime_name(part, field.name.c_str());
            if (const auto* file = std::get_if<HttpFile>(&field.value)) {
                if (file->filePath) {
                    curl_mime_filedata(part, file->filePath->c_str());
                } else {
                    curl_mime_data(part, file->data.c_str(), file->data.size());
                }
                curl_mime_type(part, file->mimeType.c_str());
                curl_mime_filename(part, file->fileName.c_str());
            } else {
                const std::string& value = std::get<std::string>(field.value);
                curl_mime_data(part, value.c_str(), value.size());
                curl_mime_type(part, "text/plain");
            }
        }
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteString);

    char errbuf[CURL_ERROR_SIZE] { };
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    auto res = curl_easy_perform(curl);
    curl_mime_free(mime);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    _http_responses.labels({std::to_string(status)}).inc();
    if (res == CURLE_OPERATION_TIMEDOUT) _timeouts.inc();

    if (res == CURLcode::CURLE_ABORTED_BY_CALLBACK
        && ((state.isEternalCancel && state.isEternalCancel->load())
            || (state.globalCancelEpoch && state.currentCancelEpoch < state.globalCancelEpoch->load()))) {
        const size_t slashPos = url.rfind('/');

        if (slashPos == std::string::npos) {
            throw RequestCancelled();
        }
        throw RequestCancelled(std::string_view(url).substr(slashPos + 1));
    }

    // If the request did not complete correctly, show the error
    // information. If no detailed error information was written to errbuf
    // show the more generic information from curl_easy_strerror instead.
    if (res != CURLE_OK) {
        size_t len = strlen(errbuf);
        std::string errmsg;
        if (len) {
            errmsg = std::string(errbuf) + ((errbuf[len - 1] != '\n') ? "\n" : "");
        } else {
            errmsg = curl_easy_strerror(res);
        }
        const std::string_view proxy = _proxyUrl ? std::string_view(_proxyUrl) : std::string_view();
        errmsg = log::redact(scrub_text(scrub_text(errmsg, url), proxy));
        const std::string method = method_of(url);
        log::write(log::Level::Warn, "bot_http", "request failed", {{"method", method}, {"reason", errmsg}});
        errors::Context context;
        context.method = method;
        throw errors::NetworkError("curl error: " + errmsg, context);
    }

    return response;
}

} // namespace cppgram::bot
