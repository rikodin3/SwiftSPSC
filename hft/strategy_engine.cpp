#include "swiftspsc/queue_common.h"
#include "hft_types.h"

#include <iostream>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <immintrin.h>

using namespace swiftspsc::ipc;

int main() {
    constexpr size_t kCapacity = 1 << 16;
    using Layout = ShmLayout<kCapacity>;
    const char* shm_name = "/hft_market_data";

    int fd = -1;
    while ((fd = shm_open(shm_name, O_RDWR, 0666)) < 0) {
        usleep(1000);
    }
    
    auto* layout = static_cast<Layout*>(mmap(NULL, sizeof(Layout), 
                                           PROT_READ | PROT_WRITE, 
                                           MAP_SHARED, fd, 0));
    
    Queue q;
    q.ctrl = &layout->ctrl;
    q.buffer = layout->buffer;
    q.capacity = kCapacity;
    q.mask = kCapacity - 1;

    uint64_t head = 0;
    uint64_t tail = 0;
    Item item;
    uint64_t count = 0;

    std::cout << "[Strategy] Listening for market data..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    while (true) {
        if (dequeue(q, head, tail, item)) {
            count++;
            // Process HFT Strategy Logic here...
            // e.g. if (item.data > threshold) send_order();
            
            if (count % 1'000'000 == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                std::cout << "[Strategy] Processed " << count << " updates. Current rate: " 
                          << (count * 1000.0 / diff) / 1e6 << " M msgs/sec" << std::endl;
            }
        } else if (layout->ctrl.done.load(std::memory_order_acquire)) {
            // Final check of tail
            tail = layout->ctrl.tail.load(std::memory_order_acquire);
            if (head == tail) break;
        } else {
            _mm_pause();
        }
    }

    std::cout << "[Strategy] Summary: Total processed: " << count << std::endl;
    
    munmap(layout, sizeof(Layout));
    close(fd);
    return 0;
}
