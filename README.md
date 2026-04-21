# SwiftSPSC

A clean, high-performance SPSC (single-producer single-consumer) lock-free queue repository based on the queue behavior from your existing producer, consumer, and queue common code.

This repo provides:

- A reusable in-process queue API: `swiftspsc::SPSCQueue<T>`.
- A cleaned IPC/shared-memory queue path using `queue_common.h` primitives.
- Producer and consumer examples.
- Throughput and latency benchmarks.
- A smoke test.

## Table of Contents

- [Usage](#usage)
- [IPC Usage](#ipc-usage)
- [Benchmarks](#benchmarks)
- [Implementation](#implementation)
- [Building](#building)
- [Installing](#installing)
- [Sources](#sources)

## Usage

### In-Process Queue (Threads)

`SPSCQueue` requires capacity to be a non-zero power of two.

```cpp
#include "swiftspsc/spsc_queue.h"

swiftspsc::SPSCQueue<std::uint64_t> queue(1 << 20);

queue.push(123);

std::uint64_t value = 0;
if (queue.try_pop(value)) {
    // value == 123
}
```

Supported operations:

- `try_push(const T&)`
- `try_push(T&&)`
- `try_emplace(Args&&...)`
- `push(...)` / `emplace(...)` (spin until success)
- `try_pop(T&)`
- `pop(T&)` (spin until success)
- `size()` / `empty()` / `capacity()`

## IPC Usage

The IPC path keeps your original queue strategy and naming style, cleaned into `include/swiftspsc/queue_common.h`.

- Producer example: `examples/ipc/producer.cpp`
- Consumer example: `examples/ipc/consumer.cpp`

Linux run flow:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# terminal 1
./build/swiftspsc_ipc_consumer /swiftspsc_queue

# terminal 2
./build/swiftspsc_ipc_producer /swiftspsc_queue 100000000
```

The producer creates shared memory, writes items, and sets a done flag.
The consumer waits for producer readiness, drains until done, and reports throughput.

## Benchmarks

Included benchmarks:

- `benchmarks/thread_throughput.cpp`
- `benchmarks/thread_latency.cpp`

Run examples:

```bash
./build/swiftspsc_bench_throughput 100000000 0 1
./build/swiftspsc_bench_latency
```

Important benchmark notes:

- Use release builds (`-DCMAKE_BUILD_TYPE=Release`).
- Prefer isolated cores / pinned threads for low-noise results.
- Run multiple iterations and compare percentile latencies, not just averages.

## Implementation

Core implementation details:

- Fixed-capacity ring buffer with power-of-two masking.
- Lock-free SPSC model with atomic `head` and `tail` indices.
- Acquire/release ordering for correctness and visibility.
- Producer-side cached head and consumer-side cached tail to reduce atomic traffic.
- IPC mode preserves batch publication behavior from your original queue logic.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Installing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Headers install under your CMake include install prefix.

## Sources

Inspiration and references:

- [Rigtorp SPSCQueue](https://github.com/rigtorp/SPSCQueue)
- [MoodyCamel ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue)
- [Folly ProducerConsumerQueue](https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h)
- [Boost.Lockfree SPSC Queue](https://www.boost.org/doc/libs/1_60_0/boost/lockfree/spsc_queue.hpp)
