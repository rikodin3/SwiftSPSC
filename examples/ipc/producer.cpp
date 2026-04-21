#include "swiftspsc/queue_common.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <immintrin.h>
#include <iostream>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::uint64_t parse_or_default(int argc, char** argv, int index, std::uint64_t fallback)
{
    if (argc <= index) {
        return fallback;
    }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(argv[index], &end, 10);
    if (end == argv[index] || *end != '\0') {
        return fallback;
    }

    return static_cast<std::uint64_t>(value);
}

}  // namespace

int main(int argc, char** argv)
{
    using namespace swiftspsc::ipc;

    constexpr std::size_t kCapacity = kDefaultQueueCapacity;
    using Layout = ShmLayout<kCapacity>;

    const char* shm_name = (argc > 1) ? argv[1] : kDefaultShmName;
    const std::uint64_t message_count = parse_or_default(argc, argv, 2, 100'000'000ULL);

    if (shm_unlink(shm_name) == -1 && errno != ENOENT) {
        perror("shm_unlink");
        return 1;
    }

    const int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(fd, sizeof(Layout)) == -1) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    void* ptr = mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    auto* layout = static_cast<Layout*>(ptr);
    layout->ctrl.head.store(0, std::memory_order_relaxed);
    layout->ctrl.tail.store(0, std::memory_order_relaxed);
    layout->ctrl.done.store(0, std::memory_order_relaxed);

    Queue q;
    q.ctrl = &layout->ctrl;
    q.buffer = layout->buffer;
    q.capacity = kCapacity;
    q.mask = kCapacity - 1;

    ProducerCtx ctx;

    std::cout << "Producer started. Sending " << message_count << " items..." << std::endl;

    for (std::uint64_t i = 1; i <= message_count; ++i) {
        Item item{i};
        while (!enqueue(q, ctx, item)) {
            _mm_pause();
        }
    }

    q.ctrl->tail.store(ctx.local_tail, std::memory_order_release);
    q.ctrl->done.store(1, std::memory_order_release);

    std::cout << "Producer finished." << std::endl;

    munmap(ptr, sizeof(Layout));
    close(fd);
    return 0;
}
