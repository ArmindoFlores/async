#include "heap.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define PARENT(i) (((i) - 1) / 2)
#define LEFT(i)   (2 * (i) + 1)
#define RIGHT(i)  (2 * (i) + 2)

struct heap_s {
    void *data; 
    size_t size;
    size_t capacity;
    size_t element_size;
    uint64_t (*priority) (void*); 
};

heap heap_create(size_t initial_size, size_t element_size, uint64_t (*priority) (void*)) {
    struct heap_s *h = malloc(sizeof(struct heap_s));
    if (!h) return NULL;
    h->data = malloc(initial_size * element_size);
    if (!h->data) { 
        free(h); 
        return NULL;
    }
    h->size = 0;
    h->capacity = initial_size;
    h->element_size = element_size;
    h->priority = priority;
    return h;
}

void heap_destroy(heap h) {
    if (!h) return;
    free(h->data);
    free(h);
}

int heap_empty(heap h) {
    return h->size == 0;
}

void *heap_min(heap h) {
    return (h->size > 0) ? h->data : NULL;
}

static int swap(heap h, size_t a, size_t b) {
    void *buffer = malloc(h->element_size);
    if (buffer == NULL) {
        return -1;
    }
    memcpy(buffer, (char*) h->data + a * h->element_size, h->element_size);
    memcpy((char*) h->data + a * h->element_size, (char*) h->data + b * h->element_size, h->element_size);
    memcpy((char*) h->data + b * h->element_size, buffer, h->element_size);
    free(buffer);
    return 0;
}

static int heapify_up(heap h, size_t i) {
    while (i > 0) {
        size_t p = PARENT(i);
        if (h->priority((char*) h->data + i * h->element_size) >= h->priority((char*) h->data + p * h->element_size)) break;
        if (swap(h, i, p) == -1) return -1;
        i = p;
    }
    return 0;
}

static int heapify_down(heap h, size_t i) {
    while (1) {
        size_t l = LEFT(i), r = RIGHT(i), smallest = i;
        if (l < h->size && h->priority((char*) h->data + l * h->element_size) < h->priority((char*) h->data + smallest * h->element_size))
            smallest = l;
        if (r < h->size && h->priority((char*) h->data + r * h->element_size) < h->priority((char*) h->data + smallest * h->element_size))
            smallest = r;
        if (smallest == i) break;
        if (swap(h, i, smallest) == -1) {
            return -1;
        }
        i = smallest;
    }
    return 0;
}

int heap_insert(heap h, void* element) {
    if (h->size == h->capacity) {
        size_t newcap = h->capacity * 2;
        void *newdata = realloc(h->data, newcap * h->element_size);
        if (!newdata) return -1;
        h->data = newdata;
        h->capacity = newcap;
    }
    memcpy((char*) h->data + (h->size) * h->element_size, element, h->element_size);
    if (heapify_up(h, h->size) == -1) {
        return -1;
    }
    h->size++;
    return 0;
}

int heap_pop(heap h) {
    if (h->size == 0) return -1;

    memcpy(h->data, (char*) h->data + (h->size - 1) * h->element_size, h->element_size);

    h->size--;
    if (h->size > 0) return heapify_down(h, 0);
    return 0;
}
