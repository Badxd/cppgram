#pragma once

#include <string>

namespace cppgram {

// Production Telegram DC addresses (port 443). Returns an empty string for an unknown DC id.
// DC1 149.154.175.53, DC2 149.154.167.51, DC3 149.154.175.100, DC4 149.154.167.91, DC5 91.108.56.130.
std::string dc_address(int dc_id);

// Reads the DC number from a migrate error such as PHONE_MIGRATE_5, NETWORK_MIGRATE_3 or USER_MIGRATE_4.
// Returns 0 when the message is not a migrate error.
int migrate_dc_from_error(const std::string& error_message);

}  // namespace cppgram
