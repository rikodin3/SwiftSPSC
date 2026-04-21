cmake_minimum_required(VERSION 3.16)
project(SwiftSPSC VERSION 0.1.0 LANGUAGES CXX)

option(SWIFTSPSC_BUILD_EXAMPLES "Build SwiftSPSC examples" ON)
option(SWIFTSPSC_BUILD_BENCHMARKS "Build SwiftSPSC benchmarks" ON)
option(SWIFTSPSC_BUILD_TESTS "Build SwiftSPSC tests" ON)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)

add_library(swiftspsc INTERFACE)
add_library(swiftspsc::swiftspsc ALIAS swiftspsc)

target_include_directories(swiftspsc
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(swiftspsc INTERFACE cxx_std_20)

if(SWIFTSPSC_BUILD_EXAMPLES)
    add_executable(swiftspsc_thread_example examples/thread/basic.cpp)
    target_link_libraries(swiftspsc_thread_example PRIVATE swiftspsc)
    swiftspsc_enable_warnings(swiftspsc_thread_example)

    if(UNIX)
        add_executable(swiftspsc_ipc_producer examples/ipc/producer.cpp)
        add_executable(swiftspsc_ipc_consumer examples/ipc/consumer.cpp)

        target_link_libraries(swiftspsc_ipc_producer PRIVATE swiftspsc)
        target_link_libraries(swiftspsc_ipc_consumer PRIVATE swiftspsc)

        swiftspsc_enable_warnings(swiftspsc_ipc_producer)
        swiftspsc_enable_warnings(swiftspsc_ipc_consumer)
    endif()
endif()

if(SWIFTSPSC_BUILD_BENCHMARKS)
    add_executable(swiftspsc_bench_throughput benchmarks/thread_throughput.cpp)
    add_executable(swiftspsc_bench_latency benchmarks/thread_latency.cpp)

    target_link_libraries(swiftspsc_bench_throughput PRIVATE swiftspsc)
    target_link_libraries(swiftspsc_bench_latency PRIVATE swiftspsc)

    swiftspsc_enable_warnings(swiftspsc_bench_throughput)
    swiftspsc_enable_warnings(swiftspsc_bench_latency)
endif()

if(SWIFTSPSC_BUILD_TESTS)
    enable_testing()

    add_executable(swiftspsc_smoke_test tests/spsc_queue_smoke.cpp)
    target_link_libraries(swiftspsc_smoke_test PRIVATE swiftspsc)
    swiftspsc_enable_warnings(swiftspsc_smoke_test)

    add_test(NAME swiftspsc_smoke_test COMMAND swiftspsc_smoke_test)
endif()

install(TARGETS swiftspsc EXPORT swiftspscTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
    EXPORT swiftspscTargets
    FILE swiftspscTargets.cmake
    NAMESPACE swiftspsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/swiftspsc
)
