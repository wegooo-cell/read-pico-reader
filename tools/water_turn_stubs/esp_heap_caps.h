#pragma once
#include <stddef.h>
#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int water_test_alloc_fail;
static inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps) {
    (void)caps;
    if (water_test_alloc_fail) return NULL;
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    return aligned_alloc(alignment, size);
#endif
}
static inline void heap_caps_free(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
