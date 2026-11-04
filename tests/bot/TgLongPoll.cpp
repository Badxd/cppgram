#include <gtest/gtest.h>

#include "cppgram/bot/Bot.h"
#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/TgLongPoll.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

class BlockingHttpClient final : public cppgram::bot::HttpClient {
public:
    std::string makeRequest(const std::string&, std::span<const cppgram::bot::HttpFormField>) const override {
        const auto cancelEpoch = _cancelEpoch.load();
        requestStarted.store(true);
        requestStarted.notify_one();

        while (cancelEpoch == _cancelEpoch.load()) {
            std::this_thread::yield();
        }

        throw cppgram::bot::RequestCancelled();
    }

    mutable std::atomic<bool> requestStarted { false };
};

class FailingHttpClient final : public cppgram::bot::HttpClient {
public:
    std::string makeRequest(const std::string&, std::span<const cppgram::bot::HttpFormField>) const override {
        throw std::runtime_error("network error");
    }
};

class SequencedHttpClient final : public cppgram::bot::HttpClient {
public:
    std::string makeRequest(const std::string&, std::span<const cppgram::bot::HttpFormField> fields) const override {
        requests.emplace_back(fields.begin(), fields.end());
        if (nextResponse == responses.size()) {
            throw std::logic_error("unexpected request");
        }
        return responses[nextResponse++];
    }

    mutable std::size_t nextResponse = 0;
    mutable std::vector<std::vector<cppgram::bot::HttpFormField>> requests;
    std::vector<std::string> responses;
};

} // namespace

TEST(TgLongPoll, StartDispatchesPreviousUpdatesAndAdvancesOffset) {
    SequencedHttpClient httpClient;
    httpClient.responses = {
        R"({"ok":true,"result":[{"update_id":7,"message":{"message_id":1,"date":2,"chat":{"id":3,"type":"private"},"text":"hello"}}]})",
        R"({"ok":true,"result":[]})",
    };
    cppgram::bot::Bot bot("token", httpClient, "url");
    std::shared_ptr<cppgram::bot::Message> receivedMessage;
    bot.getEvents().onAnyMessage([&](const auto& message) {
        receivedMessage = message;
    });
    auto allowedUpdates = std::make_shared<std::vector<std::string>>(std::initializer_list<std::string> { "message" });
    cppgram::bot::TgLongPoll longPoll(bot, 25, 0, allowedUpdates);

    longPoll.start();
    EXPECT_EQ(receivedMessage, nullptr);
    longPoll.start();

    ASSERT_TRUE(receivedMessage);
    ASSERT_TRUE(receivedMessage->text);
    EXPECT_EQ(*receivedMessage->text, "hello");
    ASSERT_EQ(httpClient.requests.size(), 2);
    const auto offset
        = std::find_if(httpClient.requests[1].begin(), httpClient.requests[1].end(), [](const auto& field) {
              return field.name == "offset";
          });
    ASSERT_NE(offset, httpClient.requests[1].end());
    EXPECT_EQ(std::get<std::string>(offset->value), "8");
}

TEST(TgLongPoll, StopCancelsActiveRequest) {
    BlockingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);
    std::exception_ptr exception;

    std::thread thread([&] {
        try {
            longPoll.startLoop({ }, { });
        } catch (...) {
            exception = std::current_exception();
        }
    });

    httpClient.requestStarted.wait(false);
    EXPECT_TRUE(longPoll.isRunning());

    longPoll.stop();
    thread.join();

    EXPECT_FALSE(longPoll.isRunning());
    EXPECT_EQ(exception, nullptr);
}

TEST(TgLongPoll, StartLoopRejectsConcurrentInvocation) {
    BlockingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);
    std::exception_ptr exception;
    std::thread thread([&] {
        try {
            longPoll.startLoop({ }, { });
        } catch (...) {
            exception = std::current_exception();
        }
    });

    httpClient.requestStarted.wait(false);

    EXPECT_THROW(longPoll.startLoop({ }, { }), std::logic_error);

    longPoll.stop();
    thread.join();
    EXPECT_EQ(exception, nullptr);
}

TEST(TgLongPoll, StopReportsCancellationToErrorHandler) {
    BlockingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);
    std::string errorMessage;

    std::thread thread([&] {
        longPoll.startLoop(
            [&](const std::exception& error) {
                errorMessage = error.what();
            },
            { });
    });

    httpClient.requestStarted.wait(false);
    longPoll.stop();
    thread.join();

    EXPECT_EQ(errorMessage, "request cancelled");
}

TEST(TgLongPoll, SignalStopsLoop) {
    BlockingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);
    std::exception_ptr exception;

    std::thread thread([&] {
        try {
            longPoll.startLoop({ }, { SIGINT });
        } catch (...) {
            exception = std::current_exception();
        }
    });

    httpClient.requestStarted.wait(false);
    std::raise(SIGINT);
    thread.join();

    EXPECT_FALSE(longPoll.isRunning());
    EXPECT_EQ(exception, nullptr);
}

TEST(TgLongPoll, StartLoopPropagatesRequestErrors) {
    FailingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);

    EXPECT_THROW(longPoll.startLoop({ }, { }), std::runtime_error);
    EXPECT_FALSE(longPoll.isRunning());
}

TEST(TgLongPoll, StartLoopHandlesRequestErrors) {
    FailingHttpClient httpClient;
    cppgram::bot::Bot bot("token", httpClient, "url");
    cppgram::bot::TgLongPoll longPoll(bot);
    std::string errorMessage;

    longPoll.startLoop(
        [&](const std::exception& error) {
            errorMessage = error.what();
            longPoll.stop();
        },
        { });

    EXPECT_EQ(errorMessage, "network error");
    EXPECT_FALSE(longPoll.isRunning());
}
