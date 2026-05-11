#include <unistd.h>
#include <iostream>
#include <cstring>
#include <pthread.h>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <cstdlib> // For std::malloc and std::free

// ==========================================
// 1. CUSTOM ALLOCATOR IMPLEMENTATION
// ==========================================

typedef char ALIGN[16];     

union header_t {
    struct {
        size_t size;             
        size_t requested_size;   
        bool is_free;
        union header_t* next;    
    } s;
    ALIGN stub;
};

header_t *head = nullptr, *tail = nullptr;
pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER; 

header_t* get_free_block(size_t t) {
    header_t* curr = head;
    while (curr) {
        if (curr->s.is_free && curr->s.size >= t) {
            return curr;
        }
        curr = curr->s.next;
    }
    return nullptr;
}

void *my_malloc(size_t size) {
    size_t total_size;
    void *block;
    header_t* header;

    if (!size) return NULL;
        
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    
    if (header) {
        header->s.is_free = false;
        header->s.requested_size = size; 
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
    }

    total_size = sizeof(header_t) + size;
    block = sbrk(total_size);
    
    if (block == (void*)-1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return nullptr;
    }
    
    header = (header_t*)block;
    header->s.size = size;
    header->s.requested_size = size; 
    header->s.is_free = false;
    header->s.next = NULL;
    
    if (!head) head = header;
    if (tail) tail->s.next = header;
    tail = header;
    
    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(header + 1);
}

void my_free(void* block) {
    header_t *header, *tmp;
    void *programbreak;

    if (!block) return;
        
    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1;

    programbreak = sbrk(0);
    if ((char*)block + header->s.size == programbreak) {
        if (head == tail) {
            head = tail = NULL;
        } else {
            tmp = head;
            while (tmp) {
                if (tmp->s.next == tail) {
                    tmp->s.next = NULL;
                    tail = tmp;
                }
                tmp = tmp->s.next;
            }
        }
        sbrk(0 - sizeof(header_t) - header->s.size);
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }
    
    header->s.is_free = true;
    pthread_mutex_unlock(&global_malloc_lock);
}

void* my_realloc(void* block, size_t size) {
    header_t* header;
    void* ret;
    
    if (!block || !size) return my_malloc(size);
        
    header = (header_t*)block - 1;
    if (header->s.size >= size) {
        header->s.requested_size = size; 
        return block;
    }
    
    ret = my_malloc(size);
    if (ret) {
        memcpy(ret, block, header->s.size);
        my_free(block); 
    }
    return ret;
}

void *my_calloc(size_t num, size_t nsize) {
    size_t size;
    void *block;
    
    if (!num || !nsize) return NULL;
        
    size = num * nsize;
    if (nsize != size / num) return NULL;
        
    block = my_malloc(size); 
    if (!block) return NULL;
        
    memset(block, 0, size);
    return block;
}

void print_fragmentation_stats() {
    pthread_mutex_lock(&global_malloc_lock);
    
    size_t total_heap_size = 0;
    size_t total_free_bytes = 0;       
    size_t total_allocated_capacity = 0;
    size_t total_requested_bytes = 0;  
    
    header_t* curr = head;
    
    while(curr) {
        total_heap_size += curr->s.size;
        if(curr->s.is_free) {
            total_free_bytes += curr->s.size;
        } else {
            total_allocated_capacity += curr->s.size;
            total_requested_bytes += curr->s.requested_size;
        }
        curr = curr->s.next;
    }
    pthread_mutex_unlock(&global_malloc_lock);
    
    size_t wasted_internal_bytes = total_allocated_capacity - total_requested_bytes;
    double internal_frag_ratio = total_allocated_capacity > 0 ? ((double)wasted_internal_bytes / total_allocated_capacity) * 100.0 : 0.0;
    double external_frag_ratio = total_heap_size > 0 ? ((double)total_free_bytes / total_heap_size) * 100.0 : 0.0;

    std::cout << "\n--- Custom Allocator Fragmentation Stats ---\n";
    std::cout << "Internal Fragmentation: " << internal_frag_ratio << "%\n";
    std::cout << "External Fragmentation: " << external_frag_ratio << "%\n";
    std::cout << "--------------------------------------------\n";
}


// ==========================================
// 2. BENCHMARKING SUITE
// ==========================================

typedef void* (*MallocFunc)(size_t);
typedef void (*FreeFunc)(void*);

struct AllocationRequest {
    size_t size;
    void* ptr;
};

void run_benchmark(std::string name, MallocFunc test_malloc, FreeFunc test_free, int iterations) {
    std::cout << "========================================\n";
    std::cout << "Running Benchmark: " << name << "\n";
    std::cout << "========================================\n";

    std::mt19937 gen(42); 
    std::uniform_int_distribution<size_t> size_dist(8, 8192);
    
    std::vector<AllocationRequest> requests(iterations);
    for (int i = 0; i < iterations; ++i) {
        requests[i].size = size_dist(gen);
        requests[i].ptr = nullptr;
    }

    void* heap_start = sbrk(0);

    // Timing Malloc
    auto start_alloc = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        requests[i].ptr = test_malloc(requests[i].size);
    }
    auto end_alloc = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> alloc_time = end_alloc - start_alloc;

    // Memory Check
    void* heap_end = sbrk(0);
    size_t total_heap_growth = (char*)heap_end - (char*)heap_start;
    
    size_t requested_bytes = 0;
    for (const auto& req : requests) requested_bytes += req.size;

    // Timing Free (pseudo-random order to induce fragmentation)
    auto start_free = std::chrono::high_resolution_clock::now();
    for (int i = iterations - 1; i >= 0; i -= 2) {
        if(requests[i].ptr) test_free(requests[i].ptr);
    }
    for (int i = iterations - 2; i >= 0; i -= 2) {
        if(requests[i].ptr) test_free(requests[i].ptr);
    }
    auto end_free = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> free_time = end_free - start_free;

    // Print Results
    double total_time = alloc_time.count() + free_time.count();
    double alloc_latency_ns = (alloc_time.count() * 1e9) / iterations;
    double free_latency_ns = (free_time.count() * 1e9) / iterations;
    double throughput = (iterations * 2) / total_time; 

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


// ==========================================
// 3. MAIN EXECUTION
// ==========================================

int main() {
    // 50,000 iterations provides a solid, measurable workload
    int iterations = 50000; 

    // 1. Benchmark glibc's highly optimized standard allocator
    run_benchmark("glibc malloc", std::malloc, std::free, iterations);

    // 2. Benchmark your custom free-list allocator
    run_benchmark("Custom Free-List Allocator", my_malloc, my_free, iterations);
    
    // 3. Print the resulting fragmentation state of your custom allocator
    // Since the benchmark frees everything, external fragmentation will be high
    // as your allocator currently does not coalesce (merge) free blocks.
    print_fragmentation_stats(); 

    return 0;
}