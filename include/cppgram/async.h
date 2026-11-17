// cppgram - asynchronous operations with cooperative cancellation. See docs/ASYNC.md for the design.
//
// AsyncResult<T> is the handle to one operation. It is move-only and wraps a std::future. get() blocks until the
// operation finishes and rethrows its exception. cancel() requests a stop, and link() ties the operation to a
// caller's std::stop_token.
//
// Runner is a serial executor with one worker thread. It starts that thread on the first post(). Operations run
// in FIFO order. Destroying a Runner cancels every queued operation, requests a stop on the running one and joins
// the worker. Nothing runs detached.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

#include "cppgram/errors.h"

namespace cppgram::async {

template <class T>
class AsyncResult {
public:
    AsyncResult() = default;
    AsyncResult(AsyncResult&&) noexcept = default;
    AsyncResult& operator=(AsyncResult&&) noexcept = default;
    AsyncResult(const AsyncResult&) = delete;
    AsyncResult& operator=(const AsyncResult&) = delete;

    // True for a result that holds an operation and has not been consumed by get().
    bool valid() const noexcept { return future_.valid(); }

    // True once the operation has finished (successfully or not).
    bool ready() const {
        return future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void wait() const { future_.wait(); }

    // Blocks until the operation finishes. Returns its value, or rethrows its exception.
    // A cancelled operation rethrows errors::CancelledError. get() may be called once.
    T get() { return future_.get(); }

    // Requests a stop. The operation returns or throws at its next cancellation point. Before it starts, it is
    // not run at all and get() throws errors::CancelledError.
    void cancel() noexcept { source_.request_stop(); }

    // Cancels this operation as soon as `token` stops. The link lasts as long as this result does. A token that
    // can never stop adds no link.
    void link(std::stop_token token) {
        if (!token.stop_possible()) return;
        link_ = std::make_unique<std::stop_callback<std::function<void()>>>(
            std::move(token), [source = source_]() mutable { source.request_stop(); });
    }

    AsyncResult(std::future<T> future, std::stop_source source)
        : future_(std::move(future)), source_(std::move(source)) {}

private:
    std::future<T> future_;
    std::stop_source source_;
    std::unique_ptr<std::stop_callback<std::function<void()>>> link_;
};

class Runner {
public:
    Runner();
    // Cancels the queued operations, requests a stop on the running one and joins the worker.
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    // Queues f(stop_token). Its return value, or its exception, goes to the result. An exception thrown after
    // the stop was requested is reported as errors::CancelledError.
    template <class F>
    auto post(F&& f) -> AsyncResult<std::invoke_result_t<F, std::stop_token>> {
        using R = std::invoke_result_t<F, std::stop_token>;
        auto promise = std::make_shared<std::promise<R>>();
        std::stop_source source;
        AsyncResult<R> result(promise->get_future(), source);

        // std::function needs a copyable callable, so the operation is held by shared_ptr.
        auto fn = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
        Job job;
        job.source = source;
        job.run = [promise, fn](std::stop_token token) {
            try {
                if constexpr (std::is_void_v<R>) {
                    (*fn)(token);
                    promise->set_value();
                } else {
                    promise->set_value((*fn)(token));
                }
            } catch (const std::exception& e) {
                if (token.stop_requested()) {
                    promise->set_exception(std::make_exception_ptr(errors::CancelledError(e.what())));
                } else {
                    promise->set_exception(std::current_exception());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        };
        job.cancel = [promise] {
            promise->set_exception(
                std::make_exception_ptr(errors::CancelledError("operation cancelled before it started")));
        };
        enqueue(std::move(job));
        return result;
    }

private:
    struct Job {
        std::stop_source source;
        std::function<void(std::stop_token)> run;
        std::function<void()> cancel;
    };

    void enqueue(Job job);
    void worker_loop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::optional<std::stop_source> running_;
    bool shutting_down_ = false;
    std::jthread worker_;
};

} // namespace cppgram::async
