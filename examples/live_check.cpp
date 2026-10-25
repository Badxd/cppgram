// Live connectivity check for cppgram. Run it on a machine that can reach Telegram.
//
//   TG_API_ID=123456 TG_API_HASH=abcdef... TG_BOT_TOKEN=123:ABC... ./cppgram_live_check
//
// Optional:
//   TG_DCS=2,1,3,4,5   DC order to try (default 2,1,3,4,5). Migrate hints add DCs automatically.
//   TG_DEBUG=0         turn off stage logging (on by default).
//
// Each DC gets a fresh session file (live-dcN.session) so every run exercises the full handshake.
// Credentials come only from the environment and are never written to disk.

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/session.h"

namespace {

const std::map<int, std::string> kDcAddresses = {
    {1, "149.154.175.53"},
    {2, "149.154.167.51"},
    {3, "149.154.175.100"},
    {4, "149.154.167.91"},
    {5, "91.108.56.130"},
};

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::vector<int> parse_dc_list(const std::string& text) {
    std::vector<int> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        out.push_back(std::stoi(item));
    }
    return out;
}

// Returns the DC number from a *_MIGRATE_<n> error, or 0.
int migrate_target(const std::string& message) {
    const auto pos = message.find("MIGRATE_");
    if (pos == std::string::npos) return 0;
    std::size_t i = pos + 8;
    int dc = 0;
    while (i < message.size() && std::isdigit(static_cast<unsigned char>(message[i]))) {
        dc = dc * 10 + (message[i] - '0');
        ++i;
    }
    return dc;
}

}  // namespace

int main() {
    const std::string api_id_text = env_or("TG_API_ID", "");
    const std::string api_hash = env_or("TG_API_HASH", "");
    const std::string bot_token = env_or("TG_BOT_TOKEN", "");
    if (api_id_text.empty() || api_hash.empty()) {
        std::cerr << "set TG_API_ID and TG_API_HASH (TG_BOT_TOKEN optional)\n";
        return 2;
    }
    const bool debug = env_or("TG_DEBUG", "1") != "0";
    std::vector<int> queue = parse_dc_list(env_or("TG_DCS", "2,1,3,4,5"));
    std::map<int, bool> tried;

    while (!queue.empty()) {
        const int dc = queue.front();
        queue.erase(queue.begin());
        if (tried[dc]) continue;
        tried[dc] = true;

        const auto addr = kDcAddresses.find(dc);
        if (addr == kDcAddresses.end()) {
            std::cerr << "unknown DC " << dc << "\n";
            continue;
        }
        const std::string session = "live-dc" + std::to_string(dc) + ".session";
        std::error_code ec;
        std::filesystem::remove(session, ec);

        std::cout << "== trying DC " << dc << " (" << addr->second << ":443)\n";
        cppgram::ClientOptions opts;
        opts.api_id = std::stoi(api_id_text);
        opts.api_hash = api_hash;
        opts.dc_id = dc;
        opts.dc_ip = addr->second;
        opts.dc_port = 443;
        opts.session_path = session;
        opts.debug = debug;

        cppgram::Client client(opts);
        std::string stage = "connect";
        try {
            client.connect();
            std::cout << "OK connect (DC " << dc << ")\n";
            if (!bot_token.empty()) {
                stage = "login_bot";
                client.login_bot(bot_token);
                std::cout << "OK login_bot\n";
            }
            stage = "get_me";
            const cppgram::User me = client.get_me();
            std::cout << "OK get_me: id=" << me.id << " is_bot=" << (me.is_bot ? "yes" : "no")
                      << " username=" << (me.username.empty() ? "-" : me.username) << "\n";
            client.disconnect();
            std::cout << "PASS\n";
            return 0;
        } catch (const cppgram::mtproto::ConnectionTimeout& e) {
            std::cout << "FAIL stage=" << stage << " DC=" << dc << ": timeout (" << e.what()
                      << "). Host firewall or egress may block this DC.\n";
        } catch (const cppgram::mtproto::ConnectionError& e) {
            std::cout << "FAIL stage=" << stage << " DC=" << dc << ": connection error: " << e.what() << "\n";
        } catch (const cppgram::mtproto::RpcError& e) {
            const int target = migrate_target(e.what());
            std::cout << "FAIL stage=" << stage << " DC=" << dc << ": " << e.what() << "\n";
            if (target != 0 && !tried[target]) {
                std::cout << "  migrate hint -> trying DC " << target << " next\n";
                queue.insert(queue.begin(), target);
            }
        } catch (const std::exception& e) {
            std::cout << "FAIL stage=" << stage << " DC=" << dc << ": " << e.what() << "\n";
        }
        client.disconnect();
    }
    std::cout << "FAIL: no DC answered the full check\n";
    return 1;
}
