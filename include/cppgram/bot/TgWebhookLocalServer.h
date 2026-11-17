#pragma once

#include "cppgram/bot/TgWebhookServer.h"
#include "cppgram/bot/export.h"

#include <string>

#ifdef BOOST_ASIO_HAS_LOCAL_SOCKETS

namespace cppgram::bot {

/**
 * @brief This class setups HTTP server for receiving Telegram Update objects from unix socket.
 *
 * @ingroup net
 */
class CPPGRAM_API TgWebhookLocalServer : public TgWebhookServer<boost::asio::local::stream_protocol> {
public:
    TgWebhookLocalServer(const std::string& unixSocketPath, std::string path, const EventHandler& eventHandler);

    TgWebhookLocalServer(const std::string& unixSocketPath, const Bot& bot);
};

} // namespace cppgram::bot

#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
