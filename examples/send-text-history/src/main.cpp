// Sends, edits, reads history and deletes a text message in one chat.
// Environment: TG_API_ID, TG_API_HASH, TG_TARGET (username of a chat you may write to, e.g. "@mychannel").
// Assumes an authorized session file (see examples/mtproto-login). Compile-checked only in this repository.
#include <cstdlib>
#include <iostream>
#include <string>

#include "cppgram/client.h"

int main() {
    const char* api_id = std::getenv("TG_API_ID");
    const char* api_hash = std::getenv("TG_API_HASH");
    const char* target = std::getenv("TG_TARGET");
    if (api_id == nullptr || api_hash == nullptr || target == nullptr) {
        std::cerr << "set TG_API_ID, TG_API_HASH and TG_TARGET\n";
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

        const cppgram::Message sent = client.send_message(target, "cppgram example: hello");
        std::cout << "sent message id " << sent.id << "\n";

        client.edit_message(sent.chat, sent.id, "cppgram example: edited");

        // Newest first. offset_id 0 starts from the newest message.
        for (const auto& m : client.get_history(sent.chat, 5)) {
            std::cout << m.id << "\t" << (m.outgoing ? "out" : "in ") << "\t" << m.text << "\n";
        }

        client.delete_messages(sent.chat, {sent.id}, /*revoke=*/true);
        std::cout << "deleted message " << sent.id << "\n";
    } catch (const cppgram::mtproto::RpcError& e) {
        std::cerr << "telegram error: " << e.rpc_message() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
