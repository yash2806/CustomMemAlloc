// my_allocator.h
#ifndef MY_ALLOCATOR_H
#define MY_ALLOCATOR_H

#include <cstddef> // for size_t

// Public function declarations
void* my_malloc(size_t size);
void  my_free(void* block);
void* my_calloc(size_t num, size_t nsize);
void* my_realloc(void* block, size_t size);
void  print_fragmentation_stats();

#endif // MY_ALLOCATOR_H