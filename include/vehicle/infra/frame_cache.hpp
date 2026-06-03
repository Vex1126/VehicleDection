#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace vehicle::infra {

template <typename T>
class FrameCache {
public:
    explicit FrameCache(std::size_t capacity) : capacity_(capacity) {}

    void push(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) {
            return;
        }
        if (items_.size() == capacity_) {
            items_.pop_front();
        }
        items_.push_back(std::move(value));
    }

    [[nodiscard]] std::optional<T> latest() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
            return std::nullopt;
        }
        return items_.back();
    }

    [[nodiscard]] std::vector<T> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {items_.begin(), items_.end()};
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    mutable std::mutex mutex_;
};

} // namespace vehicle::infra
