#include "cppgram/mtproto/cancel.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace cppgram::mtproto {

void sleep_cancellable(std::chrono::milliseconds duration, std::stop_token stop) {
    if (stop.stop_requested()) throw OperationCancelled();
    std::mutex m;
    std::condition_variable cv;
    // The callback takes the same mutex as the waiter. A stop request therefore cannot land between the
    // waiter's check and its wait, so the notification is never lost.
    std::stop_callback wake(stop, [&m, &cv] {
        std::lock_guard<std::mutex> guard(m);
        cv.notify_all();
    });
    const auto deadline = std::chrono::steady_clock::now() + duration;
    std::unique_lock lock(m);
    while (!stop.stop_requested()) {
        if (cv.wait_until(lock, deadline) == std::cv_status::timeout) break;
    }
    if (stop.stop_requested()) throw OperationCancelled();
}

}  // namespace cppgram::mtproto
