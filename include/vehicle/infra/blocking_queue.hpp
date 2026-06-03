#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace vehicle::infra {

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(capacity) {}

    bool push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return closed_ || capacity_ == 0 || items_.size() < capacity_; });
        if (closed_) {
            return false;
        }
        if (capacity_ == 0) {
            return true;
        }
        items_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        if (items_.empty()) {
            return std::nullopt;
        }
        T value = std::move(items_.front());
        items_.pop_front();
        notFull_.notify_one();
        return value;
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    bool closed_{false};
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
};

} // namespace vehicle::infra
