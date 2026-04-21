#ifndef SWIFTSPSC_SPSC_QUEUE_H
#define SWIFTSPSC_SPSC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace swiftspsc {

inline constexpr std::size_t kCacheLineSize = 64;

template <typename T, typename Allocator = std::allocator<T>>
class SPSCQueue {
    static_assert(std::is_default_constructible_v<T>,
                  "SPSCQueue<T> requires T to be default constructible");
    static_assert(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>,
                  "SPSCQueue<T> requires T to be copy or move assignable");

public:
    explicit SPSCQueue(std::size_t capacity, const Allocator& allocator = Allocator())
        : capacity_(validate_capacity(capacity)),
          mask_(capacity_ - 1),
          buffer_(allocator)
    {
        buffer_.resize(capacity_);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    [[nodiscard]] bool try_push(const T& value)
    {
        if (is_full()) {
            return false;
        }

        buffer_[local_tail_ & mask_] = value;
        ++local_tail_;
        
        if (should_publish()) {
            publish();
        }
        return true;
    }

    [[nodiscard]] bool try_push(T&& value)
    {
        if (is_full()) {
            return false;
        }

        if constexpr (std::is_move_assignable_v<T>) {
            buffer_[local_tail_ & mask_] = std::move(value);
        } else {
            const T& as_const_ref = value;
            buffer_[local_tail_ & mask_] = as_const_ref;
        }

        ++local_tail_;
        
        if (should_publish()) {
            publish();
        }
        return true;
    }

    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args)
    {
        if (is_full()) {
            return false;
        }

        if constexpr (std::is_move_assignable_v<T>) {
            buffer_[local_tail_ & mask_] = T(std::forward<Args>(args)...);
        } else {
            T temp(std::forward<Args>(args)...);
            buffer_[local_tail_ & mask_] = temp;
        }

        ++local_tail_;
        
        if (should_publish()) {
            publish();
        }
        return true;
    }

    void push(const T& value)
    {
        while (!try_push(value)) {
            spin_wait();
        }
    }

    void push(T&& value)
    {
        while (!try_push(std::move(value))) {
            spin_wait();
        }
    }

    template <typename... Args>
    void emplace(Args&&... args)
    {
        while (!try_emplace(std::forward<Args>(args)...)) {
            spin_wait();
        }
    }

    [[nodiscard]] bool try_pop(T& out)
    {
        if (local_head_ == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (local_head_ == cached_tail_) {
                return false;
            }
        }

        if constexpr (std::is_move_assignable_v<T>) {
            out = std::move(buffer_[local_head_ & mask_]);
        } else {
            out = buffer_[local_head_ & mask_];
        }

        ++local_head_;
        head_.store(local_head_, std::memory_order_release);
        return true;
    }

    void pop(T& out)
    {
        while (!try_pop(out)) {
            spin_wait();
        }
    }

    void flush()
    {
        publish();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(tail - head);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return static_cast<std::size_t>(capacity_);
    }

private:
    [[nodiscard]] static std::uint64_t validate_capacity(std::size_t capacity)
    {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("SPSCQueue capacity must be a non-zero power of two");
        }

        return static_cast<std::uint64_t>(capacity);
    }

    [[nodiscard]] bool is_full()
    {
        if (local_tail_ - cached_head_ < capacity_) {
            return false;
        }

        cached_head_ = head_.load(std::memory_order_acquire);
        return local_tail_ - cached_head_ >= capacity_;
    }

    static void spin_wait() noexcept
    {
        std::this_thread::yield();
    }

    [[nodiscard]] bool should_publish() const noexcept
    {
        if ((local_tail_ & (kBatchSize - 1)) == 0) {
            return true;
        }
        if (local_tail_ - last_published_ >= kMaxUnpublished) {
            return true;
        }
        return false;
    }

    void publish() noexcept
    {
        tail_.store(local_tail_, std::memory_order_release);
        last_published_ = local_tail_;
    }

    static constexpr std::uint64_t kBatchSize = 64;
    static constexpr std::uint64_t kMaxUnpublished = 256;

    alignas(kCacheLineSize) std::atomic<std::uint64_t> head_{0};
    alignas(kCacheLineSize) std::atomic<std::uint64_t> tail_{0};

    alignas(kCacheLineSize) std::uint64_t local_head_{0};
    std::uint64_t cached_tail_{0};

    alignas(kCacheLineSize) std::uint64_t local_tail_{0};
    std::uint64_t cached_head_{0};
    std::uint64_t last_published_{0};

    const std::uint64_t capacity_;
    const std::uint64_t mask_;
    std::vector<T, Allocator> buffer_;
};

}  // namespace swiftspsc

#endif
