#pragma once

#include "cppgram/bot/Api.h"
#include "cppgram/bot/ApiCodec.h"
#include "cppgram/bot/Bot.h"
#include "cppgram/bot/CurlHttpClient.h"
#include "cppgram/bot/EventBroadcaster.h"
#include "cppgram/bot/EventHandler.h"
#include "cppgram/bot/HttpClient.h"
#include "cppgram/bot/HttpFormField.h"
#include "cppgram/bot/HttpServer.h"
#include "cppgram/bot/InputFile.h"
#include "cppgram/bot/Json.h"
#include "cppgram/bot/TgException.h"
#include "cppgram/bot/TgLongPoll.h"
#include "cppgram/bot/TgWebhookLocalServer.h"
#include "cppgram/bot/TgWebhookServer.h"
#include "cppgram/bot/TgWebhookTcpServer.h"
#include "cppgram/bot/Types.h"

/** @defgroup general General
 * @brief Bot lifecycle, event handling, and errors.
 */

/** @defgroup net Network
 * @brief HTTP clients, servers, and update transports.
 */

/** @defgroup api API
 * @brief API methods and objects defined by the Telegram Bot API.
 */

/** @mainpage
 * Bot API layer of cppgram (derived from tgbot-cpp, MIT).
 *
 * [GitHub](https://github.com/reo7sp/tgbot-cpp)
 */
