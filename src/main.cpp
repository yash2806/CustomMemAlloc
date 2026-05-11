#include <unistd.h>
#include <sys/syscall.h>
#include <iostream>
#include <cstring>
#include <pthread.h>

// Note: With the addition of requested_size, the struct size increases.
// You might want to adjust ALIGN to 32 depending on your target architecture's alignment needs.
typedef char ALIGN[16];     

// store size information to free up memory
// when a program request for memory -> total_size = header_size + size of prog
union header_t {
    struct {
        size_t size;             // Actual capacity of the block
        size_t requested_size;   // What the user explicitly asked for
        bool is_free;
        union header_t* next;    // Linked list pointer
    } s;
    ALIGN stub;
};

header_t *head = nullptr, *tail = nullptr;
pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER; 

// Forward declaration
header_t* get_free_block(size_t t);

void *my_malloc(size_t size) {
    size_t total_size;
    void *block;
    header_t* header;

    if (!size)
        return NULL;
        
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    
    if (header) {
        // Block acquired, release lock
        header->s.is_free = false;
        header->s.requested_size = size; // Track internal fragmentation
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
    
    if (!head)
        head = header;
    if (tail)
        tail->s.next = header;
    tail = header;
    
    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(header + 1);
}

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

void my_free(void* block) {
    header_t *header, *tmp;
    void *programbreak;

    if (!block)
        return;
        
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
    
    if (!block || !size)
        return my_malloc(size);
        
    header = (header_t*)block - 1;
    if (header->s.size >= size) {
        // Reusing the block, update the requested size for internal fragmentation stats
        header->s.requested_size = size; 
        return block;
    }
    
    ret = my_malloc(size);
    if (ret) {
        memcpy(ret, block, header->s.size);
        my_free(block); // Use custom free
    }
    return ret;
}

void *my_calloc(size_t num, size_t nsize) {
    size_t size;
    void *block;
    
    if (!num || !nsize)
        return NULL;
        
    size = num * nsize;
    if (nsize != size / num)
        return NULL;
        
    block = my_malloc(size); // Use custom malloc
    if (!block)
        return NULL;
        
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
    
    double internal_frag_ratio = 0.0;
    if (total_allocated_capacity > 0) {
        internal_frag_ratio = ((double)wasted_internal_bytes / total_allocated_capacity) * 100.0;
    }
    
    double external_frag_ratio = 0.0;
    if (total_heap_size > 0) {
        external_frag_ratio = ((double)total_free_bytes / total_heap_size) * 100.0;
    }

    std::cout << "\n--- Fragmentation Stats ---\n";
    std::cout << "Internal Fragmentation: " << internal_frag_ratio << "% (" 
              << wasted_internal_bytes << " bytes wasted inside allocated blocks)\n";
              
    std::cout << "External Fragmentation: " << external_frag_ratio << "% (" 
              << total_free_bytes << " bytes sitting free but potentially fragmented)\n";
    std::cout << "---------------------------\n";
}

int main() {
    std::cout << "Allocating block 1 (20 bytes)..." << std::endl;
    int* ptr1 = (int*)my_malloc(sizeof(int) * 5); 
    
    std::cout << "Allocating block 2 (40 bytes)..." << std::endl;
    int* ptr2 = (int*)my_malloc(sizeof(int) * 10); 
    
    print_fragmentation_stats();

    std::cout << "\nFreeing block 1..." << std::endl;
    my_free(ptr1);
    print_fragmentation_stats();

    std::cout << "\nAllocating block 3 (10 bytes) - Should reuse block 1 with internal fragmentation..." << std::endl;
    int* ptr3 = (int*)my_malloc(10); 
    print_fragmentation_stats();

    // Clean up
    my_free(ptr2);
    my_free(ptr3);
    
    return 0;
}