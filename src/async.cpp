#include "cppgram/async.h"

namespace cppgram::async {

Runner::Runner() = default;

Runner::~Runner() {
    std::deque<Job> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
        pending.swap(queue_);
        if (running_) running_->request_stop();
    }
    cv_.notify_all();
    // The worker takes nothing more after shutting_down_ is set, so these jobs are ours to complete.
    for (auto& job : pending) job.cancel();
    if (worker_.joinable()) worker_.join();
}

void Runner::enqueue(Job job) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (shutting_down_) {
        lock.unlock();
        job.cancel();
        return;
    }
    queue_.push_back(std::move(job));
    if (!worker_.joinable()) worker_ = std::jthread([this] { worker_loop(); });
    lock.unlock();
    cv_.notify_one();
}

void Runner::worker_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return shutting_down_ || !queue_.empty(); });
            if (shutting_down_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            running_ = job.source;
        }
        if (job.source.stop_requested()) {
            job.cancel();
        } else {
            job.run(job.source.get_token());
        }
        std::lock_guard<std::mutex> lock(mutex_);
        running_.reset();
    }
}

} // namespace cppgram::async
