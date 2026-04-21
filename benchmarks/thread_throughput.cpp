#include "swiftspsc/spsc_queue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

namespace {

bool pin_thread(int cpu)
{
#ifdef __linux__
    if (cpu < 0) {
        return false;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)cpu;
    return false;
#endif
}

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

int parse_cpu_or_default(int argc, char** argv, int index, int fallback)
{
    if (argc <= index) {
        return fallback;
    }

    char* end = nullptr;
    const long value = std::strtol(argv[index], &end, 10);
    if (end == argv[index] || *end != '\0') {
        return fallback;
    }

    return static_cast<int>(value);
}

}  // namespace

int main(int argc, char** argv)
{
    const std::uint64_t message_count = parse_or_default(argc, argv, 1, 100'000'000ULL);
    const int producer_cpu = parse_cpu_or_default(argc, argv, 2, 0);
    const int consumer_cpu = parse_cpu_or_default(argc, argv, 3, 1);

    swiftspsc::SPSCQueue<std::uint64_t> queue(1 << 20);
    std::atomic<bool> consumer_ready{false};
    bool producer_pinned = false;
    bool consumer_pinned = false;

    std::chrono::steady_clock::time_point measured_start;
    std::chrono::steady_clock::time_point measured_end;

    std::thread producer([&]() {
        producer_pinned = pin_thread(producer_cpu);

        while (!consumer_ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        measured_start = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i < message_count; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        consumer_pinned = pin_thread(consumer_cpu);
        consumer_ready.store(true, std::memory_order_release);

        std::uint64_t consumed = 0;
        std::uint64_t value = 0;
        while (consumed < message_count) {
            if (queue.try_pop(value)) {
                ++consumed;
            } else {
                std::this_thread::yield();
            }
        }

        measured_end = std::chrono::steady_clock::now();
    });

    producer.join();
    consumer.join();

    const double seconds = std::chrono::duration<double>(measured_end - measured_start).count();
    const double throughput_mops = (static_cast<double>(message_count) / seconds) / 1e6;

    std::cout << "SwiftSPSC throughput: " << throughput_mops << " M ops/sec" << std::endl;

#ifdef __linux__
    if (!producer_pinned || !consumer_pinned) {
        std::cerr << "Warning: thread pinning failed; benchmark may be noisy." << std::endl;
    }
#endif

    return 0;
}
