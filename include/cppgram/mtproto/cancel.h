// cppgram - cancellable waits for retry and flood-wait backoff.
#pragma once

#include <chrono>
#include <stdexcept>
#include <stop_token>

namespace cppgram::mtproto {

// Thrown when a wait was interrupted by a stop request (Client::stop()).
class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("operation cancelled") {}
};

// Sleeps for `duration`, or returns early by throwing OperationCancelled as soon as `stop` is requested.
// Returns normally only when the full duration elapsed without a stop request.
void sleep_cancellable(std::chrono::milliseconds duration, std::stop_token stop);

}  // namespace cppgram::mtproto
