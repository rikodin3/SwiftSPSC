#ifndef SWIFTSPSC_SPSC_QUEUE_H
#define SWIFTSPSC_SPSC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <memory>
#include <stdexcept>
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
        if (!reserve_enqueue_slot()) {
            return false;
        }

        store_item(value);
        commit_enqueue();
        return true;
    }

    [[nodiscard]] bool try_push(T&& value)
    {
        if (!reserve_enqueue_slot()) {
            return false;
        }

        store_item(std::forward<T>(value));
        commit_enqueue();
        return true;
    }

    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args)
    {
        if (!reserve_enqueue_slot()) {
            return false;
        }

        store_item(T(std::forward<Args>(args)...));
        commit_enqueue();
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
        return dequeue_impl(out);
    }

    void pop(T& out)
    {
        while (!try_pop(out)) {
            spin_wait();
        }
    }

    void flush() noexcept
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
    struct ProducerCtx {
        std::uint64_t local_tail = 0;
        std::uint64_t cached_head = 0;
        std::uint64_t last_published = 0;

        static constexpr std::uint64_t BATCH_SIZE = 64;
        static constexpr std::uint64_t MAX_UNPUBLISHED = 256;
    };

    [[nodiscard]] static std::uint64_t validate_capacity(std::size_t capacity)
    {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("SPSCQueue capacity must be a non-zero power of two");
        }

        return static_cast<std::uint64_t>(capacity);
    }

    [[nodiscard]] bool reserve_enqueue_slot()
    {
        const std::uint64_t used = producer_.local_tail - producer_.cached_head;
        if (used < capacity_) {
            return false;
        }

        producer_.cached_head = head_.load(std::memory_order_acquire);
        return producer_.local_tail - producer_.cached_head < capacity_;
    }

    template <typename U>
    void store_item(U&& value)
    {
        if constexpr (std::is_move_assignable_v<T>) {
            buffer_[producer_.local_tail & mask_] = std::forward<U>(value);
        } else {
            const T& as_const_ref = value;
            buffer_[producer_.local_tail & mask_] = as_const_ref;
        }
    }

    void commit_enqueue() noexcept
    {
        ++producer_.local_tail;

        bool publish_now = false;
        if ((producer_.local_tail & (ProducerCtx::BATCH_SIZE - 1)) == 0) {
            publish_now = true;
        }

        if (producer_.local_tail - producer_.last_published >= ProducerCtx::MAX_UNPUBLISHED) {
            publish_now = true;
        }

        if (publish_now) {
            publish();
        }
    }

    [[nodiscard]] bool dequeue_impl(T& out)
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

    static inline void spin_wait() noexcept
    {
        _mm_pause();
    }

    void publish() noexcept
    {
        tail_.store(producer_.local_tail, std::memory_order_release);
        producer_.last_published = producer_.local_tail;
    }

    alignas(kCacheLineSize) std::atomic<std::uint64_t> head_{0};
    alignas(kCacheLineSize) std::atomic<std::uint64_t> tail_{0};

    alignas(kCacheLineSize) std::uint64_t local_head_{0};
    std::uint64_t cached_tail_{0};

    alignas(kCacheLineSize) ProducerCtx producer_{};

    const std::uint64_t capacity_;
    const std::uint64_t mask_;
    std::vector<T, Allocator> buffer_;
};

}  // namespace swiftspsc

#endif
