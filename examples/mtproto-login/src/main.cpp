// Minimal user login over MTProto with a restorable session.
// Reads credentials from the environment only (TG_API_ID, TG_API_HASH). Never prints the code or password.
// Compile-checked by tools/check_examples.sh. Not live-tested by the repository; run it with a test account.
#include <cstdlib>
#include <iostream>
#include <string>

#include "cppgram/client.h"

namespace {

std::string prompt(const char* label) {
    std::cout << label << std::flush;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

}  // namespace

int main() {
    const char* api_id = std::getenv("TG_API_ID");
    const char* api_hash = std::getenv("TG_API_HASH");
    if (api_id == nullptr || api_hash == nullptr) {
        std::cerr << "set TG_API_ID and TG_API_HASH\n";
        return 2;
    }

    cppgram::ClientOptions options;
    options.api_id = std::stoi(api_id);
    options.api_hash = api_hash;
    options.session_path = "user.session";  // keep this file private (mode 0600 is set by the library)

    cppgram::Client client(options);
    try {
        client.connect();
        if (client.probe_session()) {
            std::cout << "session restored, already authorized\n";
        } else {
            const std::string phone = prompt("phone (international format): ");
            const auto info = client.send_login_code(phone);
            std::cout << "code sent via " << info.delivery << "\n";
            const std::string code = prompt("code: ");
            if (!client.sign_in_code(phone, code, info.phone_code_hash)) {
                const std::string password = prompt("2FA password: ");
                client.sign_in_password(password);
            }
        }
        std::cout << "state: authorized\n";
        client.run();  // returns after client.stop() from another thread or a handler
    } catch (const cppgram::mtproto::RpcError& e) {
        // rpc_message() is the Telegram name, for example PHONE_CODE_INVALID or FLOOD_WAIT_30.
        std::cerr << "login failed: " << e.rpc_message() << " (state: " << to_string(client.auth_state()) << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
