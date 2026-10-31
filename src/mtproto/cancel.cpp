#include "cppgram/mtproto/cancel.h"

#include <condition_variable>
#include <mutex>

namespace cppgram::mtproto {

void sleep_cancellable(std::chrono::milliseconds duration, std::stop_token stop) {
    if (stop.stop_requested()) throw OperationCancelled();
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock lock(m);
    // The predicate is true only after a stop request, so a timeout returns false.
    const bool stopped = cv.wait_for(lock, stop, duration, [&stop] { return stop.stop_requested(); });
    if (stopped) throw OperationCancelled();
}

}  // namespace cppgram::mtproto
