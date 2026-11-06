// Integration checks for the logging and typed-error wiring: the Client connect path, the Bot API path, the Curl
// transport and the WebApp verifier. Every test captures the log stream and asserts that no secret reached it.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "cppgram/bot/Api.h"
#include "cppgram/bot/CurlHttpClient.h"
#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/TgException.h"
#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/log.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/webapp.h"

namespace {

using namespace cppgram;

constexpr const char* kApiHash = "0123456789abcdef0123456789abcdef";
constexpr const char* kBotToken = "123456789:AAExampleSecretPartForIntegrationTest_1234567890";

// Captures every record that reaches the sink at Trace level; restores the defaults when it goes out of scope.
class Capture {
public:
    Capture() {
        log::set_level(log::Level::Trace);
        log::set_sink([this](const log::Record& record) { records.push_back(record); });
    }
    ~Capture() {
        log::set_sink({});
        log::set_level(log::Level::Warn);
    }
    std::string all_text() const {
        std::string text;
        for (const auto& record : records) {
            text += record.component + " " + record.message + " " + record.request_id;
            for (const auto& field : record.fields) text += " " + field.key + "=" + field.value;
            text += "\n";
        }
        return text;
    }

    std::vector<log::Record> records;
};

// Exposes the protected sendRequest so the test can drive the Bot API path directly.
class ApiProbe : public bot::Api {
public:
    using bot::Api::Api;
    nlohmann::json call(std::string_view method) const { return sendRequest(method, {}); }
};

class FixedHttpClient final : public bot::HttpClient {
public:
    explicit FixedHttpClient(std::string body) : body_(std::move(body)) {}
    std::string makeRequest(const std::string&, std::span<const bot::HttpFormField>) const override {
        return body_;
    }

private:
    std::string body_;
};

TEST(IntegrationLogging, ConnectFailureIsTypedAndKeepsApiHashOutOfLogs) {
    Capture capture;
    ClientOptions options;
    options.api_id = 12345;
    options.api_hash = kApiHash;
    options.dc_ip = "127.0.0.1";
    options.dc_port = 1; // nothing listens on this port
    options.connect_timeout_ms = 1000;
    options.retry.max_attempts = 1;
    options.session_path = (std::filesystem::temp_directory_path() / "cppgram_integration_test.session").string();

    Client client(options);
    EXPECT_THROW(client.connect(), mtproto::ConnectionError);

    const std::string text = capture.all_text();
    EXPECT_NE(text.find("connect failed"), std::string::npos) << text;
    EXPECT_NE(text.find("category="), std::string::npos) << text;
    EXPECT_EQ(text.find(kApiHash), std::string::npos) << text;
}

TEST(IntegrationLogging, BotApiFailureLogsMethodAndNeverTheToken) {
    Capture capture;
    FixedHttpClient http(R"({"ok":false,"error_code":401,"description":"Unauthorized"})");
    ApiProbe api(kBotToken, http, "https://api.telegram.org");

    EXPECT_THROW(api.call("getMe"), bot::TgException);

    const std::string text = capture.all_text();
    EXPECT_NE(text.find("request failed"), std::string::npos) << text;
    EXPECT_NE(text.find("method=getMe"), std::string::npos) << text;
    EXPECT_NE(text.find("error_code=401"), std::string::npos) << text;
    EXPECT_EQ(text.find(kBotToken), std::string::npos) << text;
    EXPECT_EQ(text.find("AAExampleSecretPart"), std::string::npos) << text;
}

TEST(IntegrationLogging, CurlTransportErrorHidesTheTokenInMessageAndLogs) {
    Capture capture;
    bot::CurlHttpClient http;
    const std::string url = std::string("http://127.0.0.1:1/bot") + kBotToken + "/getMe";

    try {
        http.makeRequest(url, {});
        FAIL() << "expected a transport error";
    } catch (const errors::NetworkError& error) {
        EXPECT_EQ(error.method(), "getMe");
        EXPECT_EQ(std::string(error.what()).find(kBotToken), std::string::npos) << error.what();
        EXPECT_EQ(std::string(error.what()).find("AAExampleSecretPart"), std::string::npos) << error.what();
    }

    EXPECT_EQ(capture.all_text().find(kBotToken), std::string::npos);
    EXPECT_EQ(capture.all_text().find("AAExampleSecretPart"), std::string::npos);
}

TEST(IntegrationLogging, WebAppRejectionLogsStatusWithoutTheToken) {
    Capture capture;
    const std::string init_data = std::string("auth_date=1700000000&hash=") + std::string(64, '0');

    const auto result = webapp::verify(init_data, kBotToken, 1700000001, webapp::VerifyOptions{});
    EXPECT_FALSE(result.ok());

    const std::string text = capture.all_text();
    EXPECT_NE(text.find("initData rejected"), std::string::npos) << text;
    EXPECT_NE(text.find("status="), std::string::npos) << text;
    EXPECT_EQ(text.find(kBotToken), std::string::npos) << text;
}

} // namespace
