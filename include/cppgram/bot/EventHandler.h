#pragma once

#include "cppgram/bot/EventBroadcaster.h"
#include "cppgram/bot/Types.h"
#include "cppgram/bot/export.h"

namespace cppgram::bot {

/**
 * @ingroup general
 */
class CPPGRAM_API EventHandler {
public:
    explicit EventHandler(const EventBroadcaster& broadcaster);

    void handleUpdate(const std::shared_ptr<Update>& update) const;

private:
    const EventBroadcaster& _broadcaster;

    void handleMessage(const std::shared_ptr<Message>& message) const;
};

} // namespace cppgram::bot
