#include "swiftspsc/spsc_queue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

int main()
{
    constexpr std::uint64_t kMessages = 10'000'000;

    swiftspsc::SPSCQueue<std::uint64_t> queue(1 << 20);
    std::atomic<bool> producer_done{false};

    std::uint64_t consumed = 0;

    const auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < kMessages; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        std::uint64_t value = 0;
        while (consumed < kMessages) {
            if (queue.try_pop(value)) {
                ++consumed;
            } else if (producer_done.load(std::memory_order_acquire) && queue.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Consumed: " << consumed << '\n';
    std::cout << "Throughput: " << (static_cast<double>(consumed) / seconds) / 1e6 << " M ops/sec" << '\n';

    return consumed == kMessages ? 0 : 1;
}
