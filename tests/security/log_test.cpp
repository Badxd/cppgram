#include "cppgram/log.h"

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

using namespace cppgram;

namespace {

// Captures records and restores the default logger on destruction. Every test uses one of these.
class CaptureSink {
public:
    CaptureSink() {
        previous_level_ = log::level();
        log::set_sink([this](const log::Record& r) {
            std::lock_guard lock(mutex_);
            records_.push_back(r);
        });
    }
    ~CaptureSink() {
        log::set_sink({});
        log::set_level(previous_level_);
    }
    std::vector<log::Record> records() {
        std::lock_guard lock(mutex_);
        return records_;
    }

private:
    std::mutex mutex_;
    std::vector<log::Record> records_;
    log::Level previous_level_ { log::Level::Warn };
};

constexpr char kBotToken[] = "123456:AAFakeTokenForTestOnly_0123456789abc";

} // namespace

TEST(LogTest, LevelNamesRoundTrip) {
    EXPECT_EQ(log::to_string(log::Level::Trace), "TRACE");
    EXPECT_EQ(log::to_string(log::Level::Off), "OFF");
    EXPECT_EQ(log::parse_level("debug"), log::Level::Debug);
    EXPECT_EQ(log::parse_level("WARNING"), log::Level::Warn);
    EXPECT_EQ(log::parse_level("Error"), log::Level::Error);
    EXPECT_FALSE(log::parse_level("verbose").has_value());
    EXPECT_FALSE(log::parse_level("").has_value());
}

TEST(LogTest, LevelFilteringDropsRecordsBelowTheThreshold) {
    CaptureSink capture;
    log::set_level(log::Level::Info);

    log::write(log::Level::Trace, "t", "trace");
    log::write(log::Level::Debug, "t", "debug");
    log::write(log::Level::Info, "t", "info");
    log::write(log::Level::Warn, "t", "warn");
    log::write(log::Level::Error, "t", "error");

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].message, "info");
    EXPECT_EQ(records[1].message, "warn");
    EXPECT_EQ(records[2].message, "error");
}

TEST(LogTest, OffSilencesEverything) {
    CaptureSink capture;
    log::set_level(log::Level::Off);
    log::write(log::Level::Error, "t", "should not appear");
    EXPECT_TRUE(capture.records().empty());
    EXPECT_FALSE(log::enabled(log::Level::Error));
}

TEST(LogTest, EnabledMatchesWrite) {
    log::set_level(log::Level::Warn);
    EXPECT_FALSE(log::enabled(log::Level::Info));
    EXPECT_TRUE(log::enabled(log::Level::Warn));
    EXPECT_TRUE(log::enabled(log::Level::Error));
}

TEST(LogTest, RecordKeepsComponentRequestIdAndFields) {
    CaptureSink capture;
    log::set_level(log::Level::Trace);
    log::write(log::Level::Info, "rpc", "sent", { { "dc", "2" }, { "method", "sendMessage" } }, "req-42");

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].component, "rpc");
    EXPECT_EQ(records[0].request_id, "req-42");
    ASSERT_EQ(records[0].fields.size(), 2u);
    EXPECT_EQ(records[0].fields[0].key, "dc");
    EXPECT_EQ(records[0].fields[0].value, "2");
}

TEST(LogTest, FormatIsDeterministicForAFixedTime) {
    log::Record r;
    r.time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1793721600123)); // 2026-11-04T...
    r.level = log::Level::Warn;
    r.component = "auth";
    r.request_id = "rid7";
    r.message = "retrying";
    r.fields = { { "attempt", "2" } };

    const std::string line = log::format(r);
    EXPECT_EQ(line.substr(0, 24).back(), 'Z');
    EXPECT_NE(line.find(" WARN  [auth] rid=rid7 retrying attempt=2"), std::string::npos) << line;
    EXPECT_EQ(line, log::format(r));
}

TEST(LogTest, FormatOmitsRequestIdWhenEmpty) {
    log::Record r;
    r.level = log::Level::Error;
    r.component = "x";
    r.message = "boom";
    const std::string line = log::format(r);
    EXPECT_EQ(line.find("rid="), std::string::npos);
    EXPECT_NE(line.find("ERROR [x] boom"), std::string::npos) << line;
}

TEST(RedactionTest, MasksBotTokensInFreeText) {
    const std::string out = log::redact(std::string("login with ") + kBotToken + " done");
    EXPECT_EQ(out.find("AAFakeToken"), std::string::npos);
    EXPECT_NE(out.find("<bot-token>"), std::string::npos);
    EXPECT_NE(out.find("login with"), std::string::npos);
}

TEST(RedactionTest, MasksSensitiveKeyValuePairs) {
    EXPECT_EQ(log::redact("api_hash=0123456789abcdef0123456789abcdef ok"), "api_hash=*** ok");
    EXPECT_EQ(log::redact("code=12345&dc=2"), "code=***&dc=2");
    EXPECT_EQ(log::redact("session=abc password=hunter2"), "session=*** password=***");
}

TEST(RedactionTest, LeavesNonSensitiveTextAlone) {
    EXPECT_EQ(log::redact("error_code=400 dc=2"), "error_code=400 dc=2");
    EXPECT_EQ(log::redact("plain text"), "plain text");
    EXPECT_EQ(log::redact(""), "");
}

TEST(RedactionTest, SensitiveKeysAreRecognisedCaseInsensitively) {
    EXPECT_TRUE(log::is_sensitive_key("API_HASH"));
    EXPECT_TRUE(log::is_sensitive_key("bot_token"));
    EXPECT_TRUE(log::is_sensitive_key("Auth_Key"));
    EXPECT_TRUE(log::is_sensitive_key("phone_code"));
    EXPECT_TRUE(log::is_sensitive_key("hash"));
    EXPECT_FALSE(log::is_sensitive_key("error_code"));
    EXPECT_FALSE(log::is_sensitive_key("dc_id"));
}

TEST(RedactionTest, SinkReceivesRedactedFieldsAndMessage) {
    CaptureSink capture;
    log::set_level(log::Level::Info);
    log::write(log::Level::Info, "login", std::string("token ") + kBotToken,
               { { "api_hash", "0123456789abcdef0123456789abcdef" }, { "code", "98765" }, { "dc", "4" } });

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].message.find("AAFakeToken"), std::string::npos);
    EXPECT_EQ(records[0].fields[0].value, "***");
    EXPECT_EQ(records[0].fields[1].value, "***");
    EXPECT_EQ(records[0].fields[2].value, "4");

    // The default text format must not expose any of it either.
    const std::string line = log::format(records[0]);
    EXPECT_EQ(line.find("AAFakeToken"), std::string::npos);
    EXPECT_EQ(line.find("0123456789abcdef"), std::string::npos);
    EXPECT_EQ(line.find("98765"), std::string::npos);
}

TEST(RedactionTest, RequestIdIsRedactedToo) {
    CaptureSink capture;
    log::set_level(log::Level::Info);
    log::write(log::Level::Info, "x", "m", {}, kBotToken);
    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].request_id.find("AAFakeToken"), std::string::npos);
}
