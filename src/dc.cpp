#include "cppgram/dc.h"

#include <charconv>
#include <string_view>
#include <system_error>

namespace cppgram {

std::string dc_address(int dc_id) {
    switch (dc_id) {
        case 1: return "149.154.175.53";
        case 2: return "149.154.167.51";
        case 3: return "149.154.175.100";
        case 4: return "149.154.167.91";
        case 5: return "91.108.56.130";
        default: return "";
    }
}

int migrate_dc_from_error(const std::string& error_message) {
    for (const char* tag : {"PHONE_MIGRATE_", "NETWORK_MIGRATE_", "USER_MIGRATE_"}) {
        const std::size_t pos = error_message.find(tag);
        if (pos == std::string::npos) continue;
        // Server-controlled text: parse digits without std::atoi, whose overflow is undefined behaviour.
        const std::string_view rest = std::string_view(error_message).substr(pos + std::char_traits<char>::length(tag));
        int dc = 0;
        const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), dc);
        if (ec == std::errc{} && end != rest.data() && dc >= 1 && dc <= 99) return dc;
    }
    return 0;
}

}  // namespace cppgram
