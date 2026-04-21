#ifndef SWIFTSPSC_QUEUE_COMMON_H
#define SWIFTSPSC_QUEUE_COMMON_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace swiftspsc::ipc {

inline constexpr std::size_t kCacheLine = 64;
inline constexpr const char* kDefaultShmName = "/swiftspsc_queue";
inline constexpr std::uint64_t kDefaultQueueCapacity = 1ULL << 20;

struct Item {
    std::uint64_t data{};
};

struct alignas(kCacheLine) ControlBlock {
    alignas(kCacheLine) std::atomic<std::uint64_t> head{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> tail{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> done{0};
};

struct Queue {
    ControlBlock* ctrl{};
    Item* buffer{};
    std::uint64_t capacity{};
    std::uint64_t mask{};
};

struct ProducerCtx {
    std::uint64_t local_tail = 0;
    std::uint64_t cached_head = 0;
    std::uint64_t last_published = 0;

    static constexpr std::uint64_t kBatchSize = 64;
    static constexpr std::uint64_t kMaxUnpublished = 256;
};

struct ConsumerTuning {
    static constexpr std::uint64_t kDonePollInterval = 64;
    static constexpr std::uint64_t kDonePollMask = kDonePollInterval - 1;
};

template <std::size_t Capacity = kDefaultQueueCapacity>
struct ShmLayout {
    static_assert(Capacity > 0, "Capacity must be greater than zero");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    ControlBlock ctrl;
    Item buffer[Capacity];
};

inline bool enqueue(Queue& q, ProducerCtx& ctx, const Item& item)
{
    std::uint64_t used = ctx.local_tail - ctx.cached_head;

    if (used >= q.capacity) {
        ctx.cached_head = q.ctrl->head.load(std::memory_order_acquire);

        if (ctx.local_tail - ctx.cached_head >= q.capacity) {
            return false;
        }
    }

    q.buffer[ctx.local_tail & q.mask] = item;
    ++ctx.local_tail;

    bool publish = false;
    if ((ctx.local_tail & (ProducerCtx::kBatchSize - 1)) == 0) {
        publish = true;
    }

    if (ctx.local_tail - ctx.last_published >= ProducerCtx::kMaxUnpublished) {
        publish = true;
    }

    if (publish) {
        q.ctrl->tail.store(ctx.local_tail, std::memory_order_release);
        ctx.last_published = ctx.local_tail;
    }

    return true;
}

inline bool dequeue(Queue& q,
                    std::uint64_t& local_head,
                    std::uint64_t& cached_tail,
                    Item& item)
{
    if (local_head == cached_tail) {
        cached_tail = q.ctrl->tail.load(std::memory_order_acquire);

        if (local_head == cached_tail) {
            return false;
        }
    }

    item = q.buffer[local_head & q.mask];
    ++local_head;

    q.ctrl->head.store(local_head, std::memory_order_release);
    return true;
}

}  // namespace swiftspsc::ipc

#endif
