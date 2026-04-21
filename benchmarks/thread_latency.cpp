#include "swiftspsc/spsc_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace {

struct Sample {
    std::uint64_t sequence = 0;
    std::uint64_t enqueue_ns = 0;
};

inline std::uint64_t now_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

}  // namespace

int main()
{
    constexpr std::uint64_t kWarmupSamples = 10'000;
    constexpr std::uint64_t kMeasuredSamples = 2'000'000;

    swiftspsc::SPSCQueue<Sample> queue(1 << 20);

    std::atomic<bool> producer_done{false};
    std::vector<std::uint64_t> latencies_ns;
    latencies_ns.reserve(kMeasuredSamples);

    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < kWarmupSamples + kMeasuredSamples; ++i) {
            Sample sample;
            sample.sequence = i;
            sample.enqueue_ns = now_ns();

            while (!queue.try_push(std::move(sample))) {
                std::this_thread::yield();
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        std::uint64_t consumed = 0;
        Sample sample;

        while (consumed < kWarmupSamples + kMeasuredSamples) {
            if (queue.try_pop(sample)) {
                if (consumed >= kWarmupSamples) {
                    const std::uint64_t latency = now_ns() - sample.enqueue_ns;
                    latencies_ns.push_back(latency);
                }

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

    if (latencies_ns.empty()) {
        std::cerr << "Latency benchmark captured no samples." << std::endl;
        return 1;
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());

    const auto sample_count = latencies_ns.size();
    const auto percentile_index = [](std::size_t count, std::size_t numerator, std::size_t denominator) {
        if (count == 0) {
            return std::size_t{0};
        }

        return ((count - 1) * numerator) / denominator;
    };

    const auto idx50 = percentile_index(sample_count, 50, 100);
    const auto idx90 = percentile_index(sample_count, 90, 100);
    const auto idx95 = percentile_index(sample_count, 95, 100);
    const auto idx99 = percentile_index(sample_count, 99, 100);
    const auto idx999 = percentile_index(sample_count, 999, 1000);

    const double average = static_cast<double>(
        std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0ULL)) /
        static_cast<double>(sample_count);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Latency (ns)" << std::endl;
    std::cout << "  min:    " << latencies_ns.front() << std::endl;
    std::cout << "  p50:    " << latencies_ns[idx50] << std::endl;
    std::cout << "  p90:    " << latencies_ns[idx90] << std::endl;
    std::cout << "  p95:    " << latencies_ns[idx95] << std::endl;
    std::cout << "  p99:    " << latencies_ns[idx99] << std::endl;
    std::cout << "  p99.9:  " << latencies_ns[idx999] << std::endl;
    std::cout << "  max:    " << latencies_ns.back() << std::endl;
    std::cout << "  avg:    " << average << std::endl;

    return 0;
}
