#include "swiftspsc/queue_common.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <immintrin.h>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    using namespace swiftspsc::ipc;

    constexpr std::size_t kCapacity = kDefaultQueueCapacity;
    using Layout = ShmLayout<kCapacity>;

    const char* shm_name = (argc > 1) ? argv[1] : kDefaultShmName;

    int fd = -1;
    while (true) {
        fd = shm_open(shm_name, O_RDWR, 0666);
        if (fd != -1) {
            break;
        }

        if (errno != ENOENT) {
            perror("shm_open");
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void* ptr = mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    auto* layout = static_cast<Layout*>(ptr);

    Queue q;
    q.ctrl = &layout->ctrl;
    q.buffer = layout->buffer;
    q.capacity = kCapacity;
    q.mask = kCapacity - 1;

    std::uint64_t local_head = q.ctrl->head.load(std::memory_order_acquire);
    std::uint64_t cached_tail = local_head;
    std::uint64_t count = 0;
    std::uint64_t empty_polls = 0;
    Item item{};

    std::cout << "Consumer started. Waiting for items..." << std::endl;
    const auto start = std::chrono::steady_clock::now();

    while (true) {
        if (dequeue(q, local_head, cached_tail, item)) {
            empty_polls = 0;
            ++count;
            continue;
        }

        ++empty_polls;
        if ((empty_polls & ConsumerTuning::kDonePollMask) == 0) {
            if (q.ctrl->done.load(std::memory_order_acquire)) {
                cached_tail = q.ctrl->tail.load(std::memory_order_acquire);
                if (local_head == cached_tail) {
                    break;
                }
            }
        }

        _mm_pause();
    }

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Count received: " << count << std::endl;
    std::cout << "Throughput: " << (static_cast<double>(count) / seconds) / 1e6 << " M ops/sec" << std::endl;

    munmap(ptr, sizeof(Layout));
    close(fd);
    shm_unlink(shm_name);
    return 0;
}
