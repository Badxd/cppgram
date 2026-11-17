// Independent consumer: links the installed libcppgram through its exported target and exercises offline API only.
// No network access, no credentials.
#include <cppgram/client.h>
#include <cppgram/mtproto/proxy_secret.h>
#include <cppgram/mtproto/session.h>

#include <cstdlib>
#include <cstdio>
#include <string>

int main() {
    cppgram::ClientOptions options;
    if (options.app_version != "1.0.0") {
        std::fprintf(stderr, "app_version is '%s', expected 1.0.0\n", options.app_version.c_str());
        return 1;
    }
    options.api_id = 12345;
    options.api_hash = "0123456789abcdef0123456789abcdef";
    options.session_path = "consumer-check.session";

    cppgram::mtproto::SessionData missing;
    if (cppgram::mtproto::load_session_file("consumer-does-not-exist.session", missing)) {
        std::fprintf(stderr, "load_session_file reported an existing file\n");
        return 1;
    }

    cppgram::mtproto::ProxySecret plain = cppgram::mtproto::parse_proxy_secret(std::string(32, 'a'));
    if (plain.kind != cppgram::mtproto::ProxySecretKind::Plain) {
        std::fprintf(stderr, "plain secret parsed with the wrong kind\n");
        return 1;
    }

    std::printf("consumer ok: app_version %s, offline API calls succeeded\n", options.app_version.c_str());
    return 0;
}
