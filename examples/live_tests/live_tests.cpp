// cppgram live Telegram tests.
//
// One binary, one test per name. Each test is independent and never fakes a result: it talks to real Telegram
// (or to the proxy you configure) and reports PASS, FAIL, SKIP or NOT_SUPPORTED.
//
//   cppgram_live_tests <test>      test is one of: reconnect, flood_wait, socks5, session_restore,
//                                  mtproto_proxy, padded_framing, all
//
// Exit codes: 0 PASS, 1 FAIL, 2 usage error, 3 SKIP, 4 NOT_SUPPORTED. For "all": 1 if any test FAILs, else 0.
//
// Secrets (TG_API_HASH, TG_BOT_TOKEN, proxy secret, SOCKS5 password, CPPGRAM_SESSION_PASSPHRASE) are read from
// the environment only and are never printed. Session files are written to TG_LIVE_DIR and never printed.
// See guides/LIVE_TESTS.md for prerequisites and environment variables.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/mtproto/connection.h"
#include "cppgram/mtproto/proxy_secret.h"
#include "cppgram/mtproto/session.h"
#include "cppgram/mtproto/socks5.h"
#include "cppgram/session_crypto.h"

namespace {

using namespace std::chrono_literals;

const char* const kPassphraseEnv = "CPPGRAM_SESSION_PASSPHRASE";

enum class Outcome { Pass, Fail, Skip, NotSupported };

struct Result {
    Outcome outcome;
    std::string detail;  // never contains secrets
};

const char* outcome_name(Outcome o) {
    switch (o) {
        case Outcome::Pass: return "PASS";
        case Outcome::Fail: return "FAIL";
        case Outcome::Skip: return "SKIP";
        case Outcome::NotSupported: return "NOT_SUPPORTED";
    }
    return "FAIL";
}

int exit_code(Outcome o) {
    switch (o) {
        case Outcome::Pass: return 0;
        case Outcome::Fail: return 1;
        case Outcome::Skip: return 3;
        case Outcome::NotSupported: return 4;
    }
    return 1;
}

Result pass(std::string d) { return {Outcome::Pass, std::move(d)}; }
Result fail(std::string d) { return {Outcome::Fail, std::move(d)}; }
Result skip(std::string d) { return {Outcome::Skip, std::move(d)}; }
Result unsupported(std::string d) { return {Outcome::NotSupported, std::move(d)}; }

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::optional<std::string> env_opt(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
}

// Telegram production DC addresses, same table as examples/live_check.cpp.
const std::map<int, std::string> kDcAddresses = {
    {1, "149.154.175.53"},
    {2, "149.154.167.51"},
    {3, "149.154.175.100"},
    {4, "149.154.167.91"},
    {5, "91.108.56.130"},
};

struct Config {
    std::int32_t api_id = 0;
    std::string api_hash;
    std::string bot_token;
    std::int32_t dc_id = 2;
    std::string dc_ip;
    std::string live_dir = ".";
    bool debug = false;
};

// Reads the common settings. Returns a reason string when the credentials are missing.
std::optional<std::string> load_config(Config& cfg) {
    const std::string api_id = env_or("TG_API_ID");
    cfg.api_hash = env_or("TG_API_HASH");
    cfg.bot_token = env_or("TG_BOT_TOKEN");
    cfg.live_dir = env_or("TG_LIVE_DIR", ".");
    cfg.debug = env_or("TG_DEBUG", "0") == "1";
    try {
        cfg.dc_id = std::stoi(env_or("TG_DC", "2"));
    } catch (const std::exception&) {
        return "TG_DC must be a number from 1 to 5";
    }
    const auto addr = kDcAddresses.find(cfg.dc_id);
    if (addr == kDcAddresses.end()) return "TG_DC must be a number from 1 to 5";
    cfg.dc_ip = addr->second;
    if (api_id.empty() || cfg.api_hash.empty()) return "set TG_API_ID and TG_API_HASH";
    try {
        cfg.api_id = std::stoi(api_id);
    } catch (const std::exception&) {
        return "TG_API_ID must be a number";
    }
    if (cfg.bot_token.empty()) return "set TG_BOT_TOKEN (the bot is used for login_bot)";
    std::filesystem::create_directories(cfg.live_dir);
    return std::nullopt;
}

// Tests that talk to Telegram call this after their own offline checks, so a missing credential is a SKIP.
std::optional<Result> credentials_or_skip(Config& cfg) {
    if (auto err = load_config(cfg)) return skip(*err);
    return std::nullopt;
}

cppgram::ClientOptions base_options(const Config& cfg, const std::string& session_file) {
    cppgram::ClientOptions o;
    o.api_id = cfg.api_id;
    o.api_hash = cfg.api_hash;
    o.dc_id = cfg.dc_id;
    o.dc_ip = cfg.dc_ip;
    o.dc_port = 443;
    o.session_path = (std::filesystem::path(cfg.live_dir) / session_file).string();
    o.debug = cfg.debug;
    return o;
}

// Logs in as the bot and calls get_me. Returns a short description on success. Never includes the token.
std::string login_and_get_me(cppgram::Client& client, const Config& cfg) {
    client.login_bot(cfg.bot_token);
    const cppgram::User me = client.get_me();
    if (!me.is_bot) throw std::runtime_error("get_me returned a non-bot account for a bot login");
    return "get_me ok (bot id " + std::to_string(me.id) + ")";
}

// Converts an exception from the library into a FAIL detail, labelled with the stage. Messages never hold secrets.
Result failure_from(const std::string& stage, const std::exception& e) {
    return fail(stage + ": " + e.what());
}

// ---------------------------------------------------------------------------------------------------------------
// Local TCP relay used by the reconnect test. It forwards bytes unchanged between the client and Telegram, so
// Telegram sees a normal MTProto connection. The test closes it to create a real network drop, then reopens it.
// ---------------------------------------------------------------------------------------------------------------
class TcpRelay {
public:
    TcpRelay(std::string upstream_ip, std::uint16_t upstream_port)
        : upstream_ip_(std::move(upstream_ip)), upstream_port_(upstream_port) {}

    ~TcpRelay() { stop(); }

    // Starts listening on 127.0.0.1 with an ephemeral port and returns that port.
    std::uint16_t start() {
        want_listen_ = true;
        thread_ = std::thread([this] { accept_loop(); });
        for (int i = 0; i < 200 && !listening_; ++i) std::this_thread::sleep_for(5ms);
        if (!listening_) throw std::runtime_error("relay failed to listen");
        return port_;
    }

    // Closes the listener and every open pipe, which looks like a dropped network connection to the client.
    void drop() {
        want_listen_ = false;
        close_pipes();
        for (int i = 0; i < 400 && listening_; ++i) std::this_thread::sleep_for(5ms);
    }

    // Starts listening again on the same port.
    void reopen() { want_listen_ = true; }

    void stop() {
        if (!thread_.joinable()) return;
        stop_ = true;
        thread_.join();
        close_pipes();
    }

private:
    struct Pipe {
        int client_fd = -1;
        int upstream_fd = -1;
        std::thread to_upstream;
        std::thread to_client;
    };

    static void pump(int from, int to) {
        char buf[4096];
        for (;;) {
            const ssize_t n = ::recv(from, buf, sizeof buf, 0);
            if (n <= 0) return;
            ssize_t off = 0;
            while (off < n) {
                const ssize_t w = ::send(to, buf + off, static_cast<std::size_t>(n - off), MSG_NOSIGNAL);
                if (w <= 0) return;
                off += w;
            }
        }
    }

    bool open_listener() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);  // 0 on the first call: the kernel picks a port
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd, 16) != 0) {
            ::close(fd);
            return false;
        }
        socklen_t len = sizeof addr;
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        listen_fd_ = fd;
        return true;
    }

    bool connect_upstream(int& out) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(upstream_port_);
        if (::inet_pton(AF_INET, upstream_ip_.c_str(), &addr.sin_addr) != 1 ||
            ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            ::close(fd);
            return false;
        }
        out = fd;
        return true;
    }

    void accept_loop() {
        while (!stop_) {
            if (!want_listen_ && listen_fd_ >= 0) {
                ::close(listen_fd_);
                listen_fd_ = -1;
                listening_ = false;
            }
            if (want_listen_ && listen_fd_ < 0 && !open_listener()) {
                std::this_thread::sleep_for(50ms);
                continue;
            }
            if (listen_fd_ < 0) {
                std::this_thread::sleep_for(20ms);
                continue;
            }
            listening_ = true;
            pollfd pfd{listen_fd_, POLLIN, 0};
            if (::poll(&pfd, 1, 50) <= 0 || !(pfd.revents & POLLIN)) continue;
            const int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) continue;
            int upstream = -1;
            if (!connect_upstream(upstream)) {
                ::close(client);
                continue;
            }
            auto pipe = std::make_unique<Pipe>();
            pipe->client_fd = client;
            pipe->upstream_fd = upstream;
            pipe->to_upstream = std::thread(pump, client, upstream);
            pipe->to_client = std::thread(pump, upstream, client);
            std::lock_guard<std::mutex> lock(mutex_);
            pipes_.push_back(std::move(pipe));
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        listening_ = false;
    }

    // Shuts down every pipe so both pump threads exit, then joins and closes the sockets.
    void close_pipes() {
        std::vector<std::unique_ptr<Pipe>> closing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing.swap(pipes_);
        }
        for (auto& p : closing) {
            ::shutdown(p->client_fd, SHUT_RDWR);
            ::shutdown(p->upstream_fd, SHUT_RDWR);
        }
        for (auto& p : closing) {
            if (p->to_upstream.joinable()) p->to_upstream.join();
            if (p->to_client.joinable()) p->to_client.join();
            ::close(p->client_fd);
            ::close(p->upstream_fd);
        }
    }

    std::string upstream_ip_;
    std::uint16_t upstream_port_;
    std::uint16_t port_ = 0;
    int listen_fd_ = -1;
    std::atomic<bool> want_listen_{false};
    std::atomic<bool> listening_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::unique_ptr<Pipe>> pipes_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------------------------

// 1. Reconnect after a real network drop. The client talks to Telegram through the local relay. The relay is
//    closed for a few seconds, so run() loses the connection, backs off, and reconnects when the relay reopens.
Result test_reconnect() {
    Config cfg;
    if (auto s = credentials_or_skip(cfg)) return *s;
    TcpRelay relay(cfg.dc_ip, 443);
    std::uint16_t port = 0;
    try {
        port = relay.start();
    } catch (const std::exception& e) {
        return fail(std::string("local relay: ") + e.what());
    }

    cppgram::ClientOptions o = base_options(cfg, "live-reconnect.session");
    o.dc_ip = "127.0.0.1";
    o.dc_port = port;
    o.auto_reconnect = true;
    o.max_reconnects = 10;

    cppgram::Client client(o);
    std::atomic<int> connected_events{0};
    std::atomic<int> reconnecting_events{0};
    client.on_connection_state([&](cppgram::Client&, cppgram::Client::ConnectionEvent e) {
        if (e == cppgram::Client::ConnectionEvent::Connected) ++connected_events;
        if (e == cppgram::Client::ConnectionEvent::Reconnecting) ++reconnecting_events;
    });

    std::string stage = "connect";
    try {
        client.connect();
        stage = "login_bot/get_me (before drop)";
        const std::string before = login_and_get_me(client, cfg);

        stage = "run() and drop";
        std::exception_ptr run_error;
        std::thread runner([&] {
            try {
                client.run();
            } catch (...) {
                run_error = std::current_exception();
            }
        });
        std::this_thread::sleep_for(2s);
        relay.drop();
        std::this_thread::sleep_for(4s);
        relay.reopen();

        stage = "waiting for reconnect";
        const auto deadline = std::chrono::steady_clock::now() + 90s;
        while (connected_events.load() < 2 && !run_error && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(100ms);
        }
        client.stop();
        runner.join();
        if (run_error) {
            try {
                std::rethrow_exception(run_error);
            } catch (const std::exception& e) {
                return fail("run() ended with error during reconnect: " + std::string(e.what()));
            }
        }
        if (connected_events.load() < 2) return fail("no Connected event after the drop within 90 s");
        if (reconnecting_events.load() < 1) return fail("the drop did not produce a Reconnecting event");

        stage = "get_me (after reconnect)";
        const cppgram::User me = client.get_me();
        client.disconnect();
        relay.stop();
        return pass(before + "; reconnected after drop; " + "get_me ok after reconnect (id " +
                    std::to_string(me.id) + ")");
    } catch (const std::exception& e) {
        client.stop();
        relay.stop();
        return failure_from(stage, e);
    }
}

// 2. FLOOD_WAIT. The test makes ordinary calls only and never sends extra requests to trigger a limit. PASS needs
//    a real FLOOD_WAIT that the library handled and then recovered from. Otherwise the result is SKIP.
Result test_flood_wait() {
    Config cfg;
    if (auto s = credentials_or_skip(cfg)) return *s;
    cppgram::Client client(base_options(cfg, "live-flood.session"));
    std::atomic<int> flood_events{0};
    client.on_rpc_error([&](cppgram::Client&, std::int32_t, const std::string& name) {
        if (name.rfind("FLOOD_WAIT", 0) == 0) ++flood_events;
    });
    try {
        client.connect();
        const std::string me = login_and_get_me(client, cfg);
        client.disconnect();
        if (flood_events.load() == 0) {
            return skip("no FLOOD_WAIT happened in this run; " + me +
                        ". The test does not trigger rate limits on purpose, so it cannot prove FLOOD_WAIT handling.");
        }
        return pass("FLOOD_WAIT observed " + std::to_string(flood_events.load()) +
                    " time(s) and the request still completed; " + me);
    } catch (const std::exception& e) {
        client.disconnect();
        if (flood_events.load() > 0) {
            return fail("FLOOD_WAIT observed but the request did not complete: " + std::string(e.what()));
        }
        return failure_from("login/get_me", e);
    }
}

// 3. SOCKS5 to Telegram. Needs a SOCKS5 proxy you are allowed to use.
Result test_socks5() {
    const auto host = env_opt("TG_SOCKS5_HOST");
    const auto port_text = env_opt("TG_SOCKS5_PORT");
    if (!host || !port_text) return skip("set TG_SOCKS5_HOST and TG_SOCKS5_PORT to run this test");
    Config cfg;
    if (auto s = credentials_or_skip(cfg)) return *s;

    cppgram::mtproto::Socks5Options s;
    s.proxy_host = *host;
    try {
        s.proxy_port = static_cast<std::uint16_t>(std::stoi(*port_text));
    } catch (const std::exception&) {
        return fail("TG_SOCKS5_PORT must be a number");
    }
    const auto user = env_opt("TG_SOCKS5_USER");
    const auto pass_text = env_opt("TG_SOCKS5_PASS");
    if (user != pass_text && !(user && pass_text)) {
        return fail("TG_SOCKS5_USER and TG_SOCKS5_PASS must be set together");
    }
    if (user && pass_text) s.auth = cppgram::mtproto::Socks5Auth{*user, *pass_text};

    cppgram::ClientOptions o = base_options(cfg, "live-socks5.session");
    o.socks5 = s;
    cppgram::Client client(o);
    try {
        client.connect();
        const std::string me = login_and_get_me(client, cfg);
        client.disconnect();
        return pass("connected to Telegram through SOCKS5; " + me);
    } catch (const cppgram::mtproto::Socks5Error& e) {
        client.disconnect();
        return fail("SOCKS5 stage failed (reply " + std::to_string(e.reply_code()) + "): " + e.what());
    } catch (const std::exception& e) {
        client.disconnect();
        return failure_from("connect via SOCKS5", e);
    }
}

// 4. Encrypted session restore. Requires CPPGRAM_SESSION_PASSPHRASE. Steps:
//    a) fresh login, session saved encrypted; b) wrong passphrase must be refused; c) a new Client reuses the
//    saved key with no new handshake (key bytes unchanged) and get_me works.
Result test_session_restore() {
    const auto passphrase = env_opt(kPassphraseEnv);
    if (!passphrase) return skip("set CPPGRAM_SESSION_PASSPHRASE to run this test");
    Config cfg;
    if (auto s = credentials_or_skip(cfg)) return *s;

    const std::string session_file = (std::filesystem::path(cfg.live_dir) / "live-restore.session").string();
    std::error_code ec;
    std::filesystem::remove(session_file, ec);

    std::string stage = "first login";
    try {
        {
            cppgram::Client first(base_options(cfg, "live-restore.session"));
            first.connect();
            login_and_get_me(first, cfg);
            first.disconnect();
        }

        stage = "encryption check";
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream in(session_file, std::ios::binary);
            if (!in) return fail("session file was not written");
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (!cppgram::session_crypto::is_encrypted(bytes)) return fail("session file is not encrypted");

        stage = "load saved key";
        cppgram::mtproto::SessionData saved;
        if (!cppgram::mtproto::load_session_file(session_file, saved) || saved.auth_key.size() != 256) {
            return fail("saved session has no 256-byte auth key");
        }

        stage = "wrong passphrase";
        {
            setenv(kPassphraseEnv, "wrong-passphrase-for-live-test", 1);
            bool refused = false;
            try {
                cppgram::mtproto::SessionData tmp;
                cppgram::mtproto::load_session_file(session_file, tmp);
            } catch (const cppgram::mtproto::SessionCorruptError&) {
                refused = true;
            }
            setenv(kPassphraseEnv, passphrase->c_str(), 1);
            if (!refused) return fail("a wrong passphrase was accepted");
        }

        stage = "restore";
        {
            cppgram::Client restored(base_options(cfg, "live-restore.session"));
            restored.connect();
            const cppgram::User me = restored.get_me();
            restored.disconnect();
            (void)me;
        }

        stage = "key unchanged";
        cppgram::mtproto::SessionData after;
        cppgram::mtproto::load_session_file(session_file, after);
        if (after.auth_key != saved.auth_key || after.dc_id != saved.dc_id) {
            return fail("restore created a new auth key instead of reusing the saved one");
        }
        return pass("encrypted session saved, wrong passphrase refused, restored session reused the same key and get_me ok");
    } catch (const std::exception& e) {
        return failure_from(stage, e);
    }
}

// Shared runner for the two obfuscated MTProto proxy tests. The secret is validated before any network use.
Result run_mtproto_proxy(bool require_padded, const char* session_name) {
    const auto host = env_opt("TG_MTPROTO_PROXY_HOST");
    const auto port_text = env_opt("TG_MTPROTO_PROXY_PORT");
    const auto secret = env_opt("TG_MTPROTO_PROXY_SECRET");
    if (!host || !port_text || !secret) {
        return skip("set TG_MTPROTO_PROXY_HOST, TG_MTPROTO_PROXY_PORT and TG_MTPROTO_PROXY_SECRET to run this test");
    }

    cppgram::mtproto::ProxySecret parsed;
    try {
        parsed = cppgram::mtproto::parse_proxy_secret(*secret);
    } catch (const std::exception& e) {
        // The parser's messages name the rule that failed and never repeat the secret.
        return fail(std::string("TG_MTPROTO_PROXY_SECRET was not accepted: ") + e.what());
    }
    if (parsed.kind == cppgram::mtproto::ProxySecretKind::FakeTls) {
        return unsupported("fake-TLS (ee) proxy secrets are not supported by cppgram; use a plain or dd secret");
    }
    if (require_padded && parsed.kind != cppgram::mtproto::ProxySecretKind::PaddedIntermediate) {
        return skip("this test needs a dd-prefixed (padded-intermediate) secret");
    }

    cppgram::MtprotoProxyOptions p;
    p.host = *host;
    try {
        p.port = static_cast<std::uint16_t>(std::stoi(*port_text));
    } catch (const std::exception&) {
        return fail("TG_MTPROTO_PROXY_PORT must be a number");
    }
    p.secret = *secret;

    Config cfg;
    if (auto s = credentials_or_skip(cfg)) return *s;
    cppgram::ClientOptions o = base_options(cfg, session_name);
    o.mtproto_proxy = p;
    cppgram::Client client(o);
    try {
        client.connect();
        const std::string me = login_and_get_me(client, cfg);
        client.disconnect();
        const char* framing = parsed.kind == cppgram::mtproto::ProxySecretKind::PaddedIntermediate
                                  ? "padded-intermediate"
                                  : "intermediate";
        return pass(std::string("obfuscated proxy works with ") + framing + " framing; " + me);
    } catch (const std::exception& e) {
        client.disconnect();
        return failure_from("connect via MTProto proxy", e);
    }
}

// 5. MTProto obfuscated proxy compatibility (plain or dd secret).
Result test_mtproto_proxy() {
    // Proxy settings are checked before credentials, so offline runs report SKIP or NOT_SUPPORTED without network.
    if (!env_opt("TG_MTPROTO_PROXY_HOST")) {
        return skip("set TG_MTPROTO_PROXY_HOST, TG_MTPROTO_PROXY_PORT and TG_MTPROTO_PROXY_SECRET to run this test");
    }
    return run_mtproto_proxy(false, "live-mtproto-proxy.session");
}

// 6. Padded-intermediate framing through the proxy. Needs a dd-prefixed secret.
Result test_padded_framing() {
    if (!env_opt("TG_MTPROTO_PROXY_HOST")) {
        return skip("set TG_MTPROTO_PROXY_HOST, TG_MTPROTO_PROXY_PORT and a dd-prefixed TG_MTPROTO_PROXY_SECRET");
    }
    return run_mtproto_proxy(true, "live-padded.session");
}

// ---------------------------------------------------------------------------------------------------------------

using TestFn = Result (*)();

struct TestEntry {
    const char* name;
    TestFn fn;
};

const std::vector<TestEntry> kTests = {
    {"reconnect", test_reconnect},
    {"flood_wait", test_flood_wait},
    {"socks5", test_socks5},
    {"session_restore", test_session_restore},
    {"mtproto_proxy", test_mtproto_proxy},
    {"padded_framing", test_padded_framing},
};

void print_result(const std::string& name, const Result& r) {
    std::cout << "RESULT " << name << ": " << outcome_name(r.outcome) << " - " << r.detail << "\n";
    std::cout.flush();
}

Result run_one(const TestEntry& t) {
    // Each test is isolated: an exception escaping it becomes a FAIL for that test only.
    try {
        return t.fn();
    } catch (const std::exception& e) {
        return fail(std::string("unexpected error: ") + e.what());
    }
}

void usage() {
    std::cerr << "usage: cppgram_live_tests <test>\n  tests:";
    for (const auto& t : kTests) std::cerr << " " << t.name;
    std::cerr << " all\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return 2;
    }
    const std::string which = argv[1];

    std::vector<const TestEntry*> selected;
    if (which == "all") {
        for (const auto& t : kTests) selected.push_back(&t);
    } else {
        for (const auto& t : kTests) {
            if (which == t.name) selected.push_back(&t);
        }
    }
    if (selected.empty()) {
        usage();
        return 2;
    }

    int worst = 0;
    for (const TestEntry* t : selected) {
        const Result r = run_one(*t);
        print_result(t->name, r);
        if (r.outcome == Outcome::Fail) worst = 1;
        if (selected.size() == 1) return exit_code(r.outcome);
    }
    return worst;
}
