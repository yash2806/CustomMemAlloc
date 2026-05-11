#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <sys/syscall.h>
#include "my_allocator.h"


// Define function pointers to swap between custom and glibc allocators
typedef void* (*MallocFunc)(size_t);
typedef void (*FreeFunc)(void*);

// A realistic workload structure
struct AllocationRequest {
    size_t size;
    void* ptr;
};

void run_benchmark(std::string name, MallocFunc test_malloc, FreeFunc test_free, int iterations) {
    std::cout << "========================================\n";
    std::cout << "Running Benchmark: " << name << "\n";
    std::cout << "========================================\n";

    // 1. Generate a reproducible random workload
    // Simulate an allocation-heavy workload (sizes between 8 bytes and 8 KB)
    std::mt19937 gen(42); // Fixed seed for fair comparison
    std::uniform_int_distribution<size_t> size_dist(8, 8192);
    
    std::vector<AllocationRequest> requests(iterations);
    for (int i = 0; i < iterations; ++i) {
        requests[i].size = size_dist(gen);
        requests[i].ptr = nullptr;
    }

    void* heap_start = sbrk(0);

    // 2. Measure Throughput & Latency (Allocation)
    auto start_alloc = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        requests[i].ptr = test_malloc(requests[i].size);
    }
    
    auto end_alloc = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> alloc_time = end_alloc - start_alloc;

    // 3. Measure Memory Footprint (Proxy for Fragmentation)
    void* heap_end = sbrk(0);
    size_t total_heap_growth = (char*)heap_end - (char*)heap_start;
    
    size_t requested_bytes = 0;
    for (const auto& req : requests) requested_bytes += req.size;

    // 4. Measure Throughput & Latency (Deallocation)
    // Free in a mixed/pseudo-random order to test fragmentation handling
    auto start_free = std::chrono::high_resolution_clock::now();
    
    for (int i = iterations - 1; i >= 0; i -= 2) {
        if(requests[i].ptr) test_free(requests[i].ptr);
    }
    for (int i = iterations - 2; i >= 0; i -= 2) {
        if(requests[i].ptr) test_free(requests[i].ptr);
    }
    
    auto end_free = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> free_time = end_free - start_free;

    // 5. Calculate and Print Results
    double total_time = alloc_time.count() + free_time.count();
    double alloc_latency_ns = (alloc_time.count() * 1e9) / iterations;
    double free_latency_ns = (free_time.count() * 1e9) / iterations;
    double throughput = (iterations * 2) / total_time; // *2 for malloc + free

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "-> Latency (Avg Malloc): " << alloc_latency_ns << " ns/op\n";
    std::cout << "-> Latency (Avg Free):   " << free_latency_ns << " ns/op\n";
    std::cout << "-> Throughput:           " << throughput << " ops/sec\n";
    std::cout << "-> Memory Requested:     " << (requested_bytes / 1024.0 / 1024.0) << " MB\n";
    std::cout << "-> Total Heap Growth:    " << (total_heap_growth / 1024.0 / 1024.0) << " MB\n";
    
    double overhead = 0.0;
    if(requested_bytes > 0) overhead = ((double)total_heap_growth / requested_bytes - 1.0) * 100.0;
    std::cout << "-> Heap Overhead/Frag:   " << overhead << "%\n\n";
}

int main() {
    int iterations = 50000; // Adjust this based on your system speed

    // Test standard glibc allocator
    run_benchmark("glibc malloc", std::malloc, std::free, iterations);

    // Test your custom allocator
    run_benchmark("Custom Free-List Allocator", my_malloc, my_free, iterations);
    
    // Optional: Print detailed internal stats for your custom allocator
    // print_fragmentation_stats(); 

    return 0;
}