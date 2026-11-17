// Uploads a local file as a document, then downloads it again with progress output.
// Environment: TG_API_ID, TG_API_HASH, TG_TARGET, and the local file path as argv[1].
// The download goes to "<name>.downloaded" through a .part file: a failed transfer leaves no partial file.
#include <cstdlib>
#include <iostream>
#include <string>

#include "cppgram/client.h"

int main(int argc, char** argv) {
    const char* api_id = std::getenv("TG_API_ID");
    const char* api_hash = std::getenv("TG_API_HASH");
    const char* target = std::getenv("TG_TARGET");
    if (argc < 2 || api_id == nullptr || api_hash == nullptr || target == nullptr) {
        std::cerr << "usage: TG_API_ID=.. TG_API_HASH=.. TG_TARGET=.. media-transfer <file>\n";
        return 2;
    }
    cppgram::ClientOptions options;
    options.api_id = std::stoi(api_id);
    options.api_hash = api_hash;
    options.session_path = "user.session";

    cppgram::Client client(options);
    try {
        client.connect();
        if (!client.probe_session()) {
            std::cerr << "not authorized: run examples/mtproto-login first\n";
            return 1;
        }

        const cppgram::PeerRef peer = client.resolve(target);
        const cppgram::Message sent = client.send_document(peer, argv[1], "cppgram example upload");
        std::cout << "uploaded as message " << sent.id << " (" << sent.media.size << " bytes)\n";

        const std::string out = "downloaded.bin";
        const std::int64_t bytes = client.download_media(sent, out, [](std::int64_t done, std::int64_t total) {
            std::cout << "\r" << done << " / " << total << " bytes" << std::flush;
        });
        std::cout << "\nwrote " << bytes << " bytes to " << out << "\n";
    } catch (const cppgram::mtproto::RpcError& e) {
        std::cerr << "telegram error: " << e.rpc_message() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
