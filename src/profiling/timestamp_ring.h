#pragma once

// FIFO ownership of pending GPU frames. A busy ring skips sampling the incoming frame.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

class timestamp_ring {
public:
    static constexpr std::size_t capacity = 4;
    static constexpr std::uint64_t delay = 2;

    std::optional<std::size_t> reserve(const std::uint64_t frame) {
        if (size_ == capacity) { return std::nullopt; }
        const auto slot = (first_ + size_) % capacity;
        issued_[slot] = frame;
        ++size_;
        return slot;
    }

    std::optional<std::size_t> oldest(const std::uint64_t frame, const bool tail = false) const {
        if (!size_ || (!tail && frame - issued_[first_] < delay)) { return std::nullopt; }
        return first_;
    }

    void release() {
        first_ = (first_ + 1) % capacity;
        --size_;
    }

private:
    std::array<std::uint64_t, capacity> issued_{};
    std::size_t first_ = 0;
    std::size_t size_ = 0;
};
