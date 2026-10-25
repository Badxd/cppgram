#include "cppgram/bot/TgWebhookLocalServer.h"

#ifdef BOOST_ASIO_HAS_LOCAL_SOCKETS

namespace cppgram::bot {

TgWebhookLocalServer::TgWebhookLocalServer(const std::string& unixSocketPath, std::string path,
                                           const EventHandler& eventHandler)
    : TgWebhookServer(boost::asio::local::stream_protocol::endpoint(unixSocketPath), std::move(path), eventHandler) {
}

TgWebhookLocalServer::TgWebhookLocalServer(const std::string& unixSocketPath, const Bot& bot)
    : TgWebhookServer(boost::asio::local::stream_protocol::endpoint(unixSocketPath), bot) {
}

} // namespace cppgram::bot

#endif // BOOST_ASIO_HAS_LOCAL_SOCKETS
