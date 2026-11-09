// Minimal API example. It builds a Client from TG_API_ID and TG_API_HASH and stops there. It does not connect, log in,
// or send anything, so it needs no network and no phone. Build with -DBUILD_MINIMAL_EXAMPLE=ON.
#include <cstdlib>
#include <exception>
#include <iostream>

#include "cppgram/client.h"

int main() {
    const char* id_env = std::getenv("TG_API_ID");
    const char* hash_env = std::getenv("TG_API_HASH");
    if (id_env == nullptr || hash_env == nullptr || *id_env == '\0' || *hash_env == '\0') {
        std::cerr << "set TG_API_ID and TG_API_HASH\n";
        return 1;
    }
    try {
        cppgram::ClientOptions options;
        options.api_id = std::stoi(id_env);
        options.api_hash = hash_env;
        options.session_path = "minimal-example.session";
        cppgram::Client client(options);
        std::cout << "client constructed; no connection made\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
