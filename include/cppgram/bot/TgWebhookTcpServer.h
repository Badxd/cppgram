#pragma once

#include "cppgram/bot/TgWebhookServer.h"
#include "cppgram/bot/export.h"

#include <string>

namespace cppgram::bot {

/**
 * This class setups HTTP server for receiving Telegram Update objects from tcp connections.
 * @ingroup net
 */
class CPPGRAM_API TgWebhookTcpServer : public TgWebhookServer<boost::asio::ip::tcp> {
public:
    TgWebhookTcpServer(unsigned short port, std::string path, const EventHandler& eventHandler);

    TgWebhookTcpServer(unsigned short port, const Bot& bot);
};

} // namespace cppgram::bot
