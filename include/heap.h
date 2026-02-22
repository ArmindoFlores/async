#ifndef _H_ASYNC_TIMER_HEAP_
#define _H_ASYNC_TIMER_HEAP_

typedef struct heap_s* heap;

#include "heap.h"
#include <string.h>
#include <stdint.h>

heap heap_create(size_t initial_size, size_t element_size, uint64_t (*priority) (void*));
int heap_empty(heap);
void *heap_min(heap);
int heap_insert(heap, void* element);
int heap_pop(heap);
void heap_destroy(heap);


#endif