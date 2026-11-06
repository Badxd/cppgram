// cppgram - C++20 coroutine tasks. See docs/ASYNC.md ("Coroutines") for the rules.
//
// Task<T> is a lazy coroutine. Nothing runs until it is awaited or spawned. Awaiting a Task starts it with
// symmetric transfer, so a chain of awaits does not grow the stack. Spawn runs a Task on an async::Runner worker
// and returns an AsyncResult<T>, so cancellation, shutdown and exceptions work as they do for any other operation.
//
// Only Task<U> and this_task_stop_token() may be awaited inside a Task. A Task therefore always runs to completion
// on the thread that resumes it: it never parks itself in the middle of an operation. To wait for an external
// event, use an AsyncResult outside the Task instead.
#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "cppgram/async.h"

namespace cppgram::async {

template<class T>
class Task;

namespace detail {

template<class T>
struct is_task : std::false_type { };
template<class T>
struct is_task<Task<T>> : std::true_type { };

// Marker type for co_await this_task_stop_token().
struct StopTokenRequest { };

struct StopTokenAwaiter {
    const std::stop_token* token;
    bool await_ready() const noexcept {
        return true;
    } // no suspension: the token is read immediately
    void await_suspend(std::coroutine_handle<>) const noexcept {
    }
    std::stop_token await_resume() const {
        return *token;
    }
};

// State shared by every Task promise, independent of the result type.
struct TaskPromiseBase {
    std::coroutine_handle<> continuation; // the awaiting task, or nothing for a spawned or top-level task
    std::stop_token stop; // cancellation of the operation this task runs in
    std::exception_ptr error;

    std::suspend_always initial_suspend() noexcept {
        return {};
    } // lazy: the body runs when awaited or spawned

    struct FinalAwaiter {
        bool await_ready() const noexcept {
            return false;
        }
        template<class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> self) noexcept {
            // Hand control straight back to the awaiting task, or stop when nothing awaits this task.
            std::coroutine_handle<> next = self.promise().continuation;
            return next ? next : std::coroutine_handle<>(std::noop_coroutine());
        }
        void await_resume() const noexcept {
        }
    };
    FinalAwaiter final_suspend() noexcept {
        return {};
    }

    void unhandled_exception() noexcept {
        error = std::current_exception();
    }

    // Only Task awaitables and the stop-token request may appear in a Task body.
    template<class A>
    requires is_task<std::remove_cvref_t<A>>::value
    A&& await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }
    StopTokenAwaiter await_transform(StopTokenRequest) noexcept {
        return StopTokenAwaiter { &stop };
    }
    template<class A>
    requires(!is_task<std::remove_cvref_t<A>>::value && !std::is_same_v<std::remove_cvref_t<A>, StopTokenRequest>)
    void await_transform(A&&) = delete;
};

template<class T>
struct TaskPromise : TaskPromiseBase {
    std::optional<T> value;

    Task<T> get_return_object() noexcept;
    void return_value(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value.emplace(std::move(v));
    }
    T take() {
        return std::move(*value);
    }
};

template<>
struct TaskPromise<void> : TaskPromiseBase {
    Task<void> get_return_object() noexcept;
    void return_void() noexcept {
    }
    void take() noexcept {
    }
};

// Awaiter for `co_await task`. It does not own the task: the Task object does.
template<class T>
struct TaskAwaiter {
    std::coroutine_handle<TaskPromise<T>> child;

    bool await_ready() const noexcept {
        return !child || child.done();
    }

    template<class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> caller) noexcept {
        auto& promise = child.promise();
        promise.continuation = caller;
        promise.stop = caller.promise().stop; // cancellation flows from the outer task to the inner one
        return child; // symmetric transfer into the child
    }

    T await_resume() {
        if (!child)
            throw std::logic_error("co_await on an empty Task");
        auto& promise = child.promise();
        if (promise.error)
            std::rethrow_exception(promise.error);
        return promise.take();
    }
};

} // namespace detail

// Returns an awaitable that yields the stop token of the operation the current task runs in. A task that was not
// spawned gets an empty token. The token is only valid while the task runs.
inline detail::StopTokenRequest this_task_stop_token() noexcept {
    return {};
}

template<class T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::TaskPromise<T>;

    Task() noexcept = default;
    explicit Task(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {
    }
    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        reset();
    }

    // True while the task holds a coroutine that has not been moved out or consumed.
    bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }
    // True once the body has returned or thrown. A task that has been spawned or awaited is always done.
    bool done() const noexcept {
        return handle_ && handle_.done();
    }

    // Runs the task on the calling thread with the given stop token and returns its value. Used by Runner::spawn.
    // Rethrows the task's exception. The task is consumed.
    T run_inline(std::stop_token token) {
        if (!handle_)
            throw std::logic_error("run of an empty Task");
        handle_.promise().stop = std::move(token);
        handle_.resume();
        if (!handle_.done())
            throw std::logic_error("Task suspended: only Task awaits are supported");
        auto& promise = handle_.promise();
        if (promise.error)
            std::rethrow_exception(promise.error);
        if constexpr (std::is_void_v<T>) {
            reset();
        } else {
            T result = promise.take();
            reset();
            return result;
        }
    }

    // `co_await task` starts the task and returns its value. The awaiter does not take ownership.
    detail::TaskAwaiter<T> operator co_await() & noexcept {
        return detail::TaskAwaiter<T> { handle_ };
    }
    detail::TaskAwaiter<T> operator co_await() && noexcept {
        return detail::TaskAwaiter<T> { handle_ };
    }

private:
    void reset() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_ {};
};

namespace detail {

template<class T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
    return Task<T> { std::coroutine_handle<TaskPromise<T>>::from_promise(*this) };
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
    return Task<void> { std::coroutine_handle<TaskPromise<void>>::from_promise(*this) };
}

} // namespace detail

// Queues `task` on `runner`. The body runs on the runner's worker thread with the operation's stop token, which
// this_task_stop_token() returns. Cancelling the result before the task starts means the body never runs. The
// Task is destroyed on the worker, or when the runner is destroyed before it starts.
template<class T>
AsyncResult<T> spawn(Runner& runner, Task<T> task) {
    return runner.post([owned = std::move(task)](std::stop_token token) mutable -> T {
        return owned.run_inline(std::move(token));
    });
}

} // namespace cppgram::async
