// Runs the update loop and stops it cleanly on SIGINT or SIGTERM.
// The signal handler only sets a flag (async-signal-safe). A watcher thread, joined before exit, calls
// Client::stop(). stop() interrupts retry and reconnect backoff; run() returns after the current receive,
// at most keepalive_ms later.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "cppgram/client.h"

namespace {
std::atomic<bool> g_shutdown{false};
void on_signal(int) { g_shutdown.store(true); }
}  // namespace

int main() {
    const char* api_id = std::getenv("TG_API_ID");
    const char* api_hash = std::getenv("TG_API_HASH");
    if (api_id == nullptr || api_hash == nullptr) {
        std::cerr << "set TG_API_ID and TG_API_HASH\n";
        return 2;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    cppgram::ClientOptions options;
    options.api_id = std::stoi(api_id);
    options.api_hash = api_hash;
    options.session_path = "user.session";

    cppgram::Client client(options);
    client.connect();
    if (!client.probe_session()) {
        std::cerr << "not authorized: run examples/mtproto-login first\n";
        return 1;
    }
    client.on_message(cppgram::filters::text(), [](cppgram::Client&, const cppgram::Message& m) {
        std::cout << "message " << m.id << ": " << m.text << "\n";
    });

    std::thread watcher([&client] {
        while (!g_shutdown.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "shutdown requested\n";
        client.stop();
    });

    try {
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "run ended with error: " << e.what() << "\n";
        g_shutdown.store(true);
        watcher.join();
        return 1;
    }
    g_shutdown.store(true);
    watcher.join();
    std::cout << "stopped cleanly; auth state: " << to_string(client.auth_state()) << "\n";
    return 0;
}
