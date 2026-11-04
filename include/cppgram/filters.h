// cppgram - composable filters for handlers. A Predicate<T> wraps a test over one event type;
// Filter tests messages, CallbackFilter tests callback queries, InlineFilter tests inline queries.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "cppgram/types.h"

namespace cppgram {

template <class T>
class Predicate {
public:
    using Test = std::function<bool(const T&)>;

    Predicate() : test_([](const T&) { return true; }) {}
    explicit Predicate(Test test) : test_(std::move(test)) {}

    bool operator()(const T& value) const { return test_(value); }

    friend Predicate operator&&(const Predicate& a, const Predicate& b) {
        return Predicate([a, b](const T& v) { return a(v) && b(v); });
    }
    friend Predicate operator||(const Predicate& a, const Predicate& b) {
        return Predicate([a, b](const T& v) { return a(v) || b(v); });
    }
    friend Predicate operator!(const Predicate& a) {
        return Predicate([a](const T& v) { return !a(v); });
    }

private:
    Test test_;
};

using Filter = Predicate<Message>;
using CallbackFilter = Predicate<CallbackQuery>;
using InlineFilter = Predicate<InlineQuery>;

namespace filters {

inline Filter all() { return Filter(); }
inline Filter text() {
    return Filter([](const Message& m) { return !m.text.empty(); });
}
inline Filter media() {
    return Filter([](const Message& m) { return m.media.kind != MediaKind::None; });
}
inline Filter private_chat() {
    return Filter([](const Message& m) { return m.chat.kind == PeerKind::User; });
}
inline Filter group() {
    return Filter([](const Message& m) { return m.chat.kind == PeerKind::Chat || m.chat.kind == PeerKind::Channel; });
}
inline Filter outgoing() {
    return Filter([](const Message& m) { return m.outgoing; });
}
inline Filter incoming() {
    return Filter([](const Message& m) { return !m.outgoing; });
}
inline Filter reply() {
    return Filter([](const Message& m) { return m.reply_to_id.has_value(); });
}
inline Filter from_user(std::int64_t user_id) {
    return Filter([user_id](const Message& m) { return m.sender && m.sender->id == user_id; });
}
inline Filter in_chat(std::int64_t chat_id) {
    return Filter([chat_id](const Message& m) { return m.chat.id == chat_id; });
}
// Matches "/name", "/name args" and "/name@botname args". Prefix defaults to "/".
inline Filter command(std::string name, std::string prefix = "/") {
    return Filter([name = std::move(name), prefix = std::move(prefix)](const Message& m) {
        const std::string full = prefix + name;
        const std::string& t = m.text;
        if (t.compare(0, full.size(), full) != 0) {
            return false;
        }
        if (t.size() == full.size()) {
            return true;
        }
        const char next = t[full.size()];
        return next == ' ' || next == '@' || next == '\n';
    });
}
// Regular expression filter (ECMAScript syntax, std::regex_search).
Filter regex(std::string pattern);

// Callback queries whose data starts with the given prefix.
CallbackFilter callback_data_prefix(std::string prefix);
// Callback queries whose data matches the regular expression.
CallbackFilter callback_regex(std::string pattern);
// Inline queries whose text matches the regular expression.
InlineFilter inline_regex(std::string pattern);

}  // namespace filters

}  // namespace cppgram
