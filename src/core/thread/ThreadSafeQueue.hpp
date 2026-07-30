#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include "core/Types.hpp"

namespace uaro {

// A mutex-guarded queue with a condition variable: multiple producers/consumers,
// correct and simple — good enough for the low-rate hand-offs this client needs
// (the outgoing network packet queue; later, scene->worker jobs). It is NOT a
// lock-free ring; if a hot path ever needs one, swap the implementation behind
// this same interface.
//
// close() flips the queue to a drained-on-empty state and wakes every blocked
// waitPop(), so worker threads can fall out of their loop cleanly at shutdown.
template <typename T>
class ThreadSafeQueue {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(v));
        }
        cv_.notify_one();
    }

    // Block until an item is available or the queue is closed. Returns nullopt
    // only once the queue is both closed AND drained, so a consumer loop is:
    //   while (auto v = q.waitPop()) handle(*v);
    std::optional<T> waitPop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;  // closed and drained
        T v = std::move(q_.front());
        q_.pop_front();
        return v;
    }

    // Non-blocking: take one item if present, else nullopt.
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        return v;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lk(m_);
        return closed_;
    }

    usize size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
    bool closed_ = false;
};

} // namespace uaro
