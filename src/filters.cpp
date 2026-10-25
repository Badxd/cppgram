#include "cppgram/filters.h"

#include <regex>

namespace cppgram::filters {

Filter regex(std::string pattern) {
    const std::regex re(pattern);
    return Filter([re](const Message& m) { return std::regex_search(m.text, re); });
}

CallbackFilter callback_data_prefix(std::string prefix) {
    return CallbackFilter([prefix = std::move(prefix)](const CallbackQuery& q) {
        return q.data.size() >= prefix.size() &&
               std::string(q.data.begin(), q.data.begin() + static_cast<std::ptrdiff_t>(prefix.size())) == prefix;
    });
}

CallbackFilter callback_regex(std::string pattern) {
    const std::regex re(pattern);
    return CallbackFilter([re](const CallbackQuery& q) {
        const std::string data(q.data.begin(), q.data.end());
        return std::regex_search(data, re);
    });
}

InlineFilter inline_regex(std::string pattern) {
    const std::regex re(pattern);
    return InlineFilter([re](const InlineQuery& q) { return std::regex_search(q.query, re); });
}

}  // namespace cppgram::filters
