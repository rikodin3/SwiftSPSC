#include "swiftspsc/queue_common.h"
#include "hft_types.h"

#include <iostream>
#include <cstring>
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

    shm_unlink(shm_name);
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(fd, sizeof(Layout)) == -1) {
        perror("ftruncate");
        close(fd);
        return 1;
    }
    
    auto* layout = static_cast<Layout*>(mmap(NULL, sizeof(Layout), 
                                           PROT_READ | PROT_WRITE, 
                                           MAP_SHARED, fd, 0));
    
    // Init Queue
    Queue q;
    q.ctrl = &layout->ctrl;
    q.buffer = layout->buffer;
    q.capacity = kCapacity;
    q.mask = kCapacity - 1;
    
    ProducerCtx ctx;
    uint64_t seq = 0;

    std::cout << "[MD Gateway] Starting market data feed simulator..." << std::endl;

    while (seq < 10'000'000) {
        // Simulate a market update
        hft::MarketUpdate update;
        update.timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        update.sequence = ++seq;
        update.price = 150.0 + (seq % 100) * 0.01;
        update.quantity = 100 + (seq % 10);
        std::memcpy(update.symbol, "AAPL", 5);
        
        // In a real system, we'd copy 'update' into the queue buffer.
        // SwiftSPSC ipc::Item currently holds a uint64_t 'data'.
        // For a realistic HFT system, we'd cast the buffer to hft::MarketUpdate*
        // or ensure Item is large enough.
        
        Item item;
        item.data = seq; // Simplified for this demo
        
        while (!enqueue(q, ctx, item)) {
            _mm_pause();
        }

        if (seq % 1'000'000 == 0) {
            std::cout << "[MD Gateway] Published " << seq << " updates" << std::endl;
        }
    }

    q.ctrl->done.store(1, std::memory_order_release);
    std::cout << "[MD Gateway] Finished." << std::endl;
    
    munmap(layout, sizeof(Layout));
    close(fd);
    return 0;
}