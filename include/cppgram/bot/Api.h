#pragma once

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

#include "cppgram/bot/ApiMethods.inc.h"

    /**
     * @brief Downloads a Telegram file and returns its contents.
     */
    std::string downloadFile(std::string_view filePath, const std::vector<HttpFormField>& fields = { }) const;

    const HttpClient& _httpClient;

protected:
    nlohmann::json sendRequest(std::string_view method, const std::vector<HttpFormField>& fields) const;

    const std::string _token;
    const std::string _url;
};

} // namespace cppgram::bot
