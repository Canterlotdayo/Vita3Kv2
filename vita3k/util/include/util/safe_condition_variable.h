#pragma once

#include <condition_variable>
#include <mutex>
#include <system_error>

// Workaround for macOS bug where pthread_cond_wait sporadically returns EINVAL,
// causing std::condition_variable::wait to throw std::system_error.
// This drop-in replacement catches the exception and retries.
// See: https://github.com/graphia-app/graphia/issues/33

struct SafeConditionVariable {
    void notify_one() noexcept { cv.notify_one(); }
    void notify_all() noexcept { cv.notify_all(); }

    void wait(std::unique_lock<std::mutex> &lock) {
        while (true) {
            try {
                cv.wait(lock);
                return;
            } catch (const std::system_error &) {
            }
        }
    }

    template <typename Predicate>
    void wait(std::unique_lock<std::mutex> &lock, Predicate pred) {
        while (!pred()) {
            wait(lock);
        }
    }

    template <typename Rep, typename Period>
    std::cv_status wait_for(std::unique_lock<std::mutex> &lock, const std::chrono::duration<Rep, Period> &rel_time) {
        try {
            return cv.wait_for(lock, rel_time);
        } catch (const std::system_error &) {
            return std::cv_status::no_timeout;
        }
    }

    template <typename Rep, typename Period, typename Predicate>
    bool wait_for(std::unique_lock<std::mutex> &lock, const std::chrono::duration<Rep, Period> &rel_time, Predicate pred) {
        try {
            return cv.wait_for(lock, rel_time, pred);
        } catch (const std::system_error &) {
            return pred();
        }
    }

private:
    std::condition_variable cv;
};
