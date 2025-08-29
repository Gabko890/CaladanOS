#include <simple_malloc.h>
#include <cldtypes.h>

static char *heap_current = NULL;
static char *heap_end = NULL;

void simple_malloc_init(void) {
    heap_current = (char*)0xFFFFFFFF90000000ULL;  // Virtual heap address
    heap_end = (char*)(0xFFFFFFFF90000000ULL + (4ULL << 20));  // 4MB heap
}

void* simple_malloc(size_t size) {
    if (!heap_current) return NULL;  // Not initialized
    if (size == 0) return NULL;
    size = (size + 15) & ~15ULL;  // 16-byte align
    if (heap_current + size > heap_end) return NULL;  // Out of memory
    char *result = heap_current;
    heap_current += size;
    return result;
}

void simple_free(void* ptr) {
    // Simple allocator doesn't actually free - just a stub
    (void)ptr;
}