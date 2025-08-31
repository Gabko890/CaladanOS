#ifndef KMALLOC_H
#define KMALLOC_H

#include <cldtypes.h>
#include <memory_info.h>
#include <kheap.h>

typedef struct free_block {
    size_t size;
    struct free_block* next;
} free_block_t;

void kmalloc_init(struct memory_info* minfo);

void* kmalloc(size_t size);
void* krealloc(void* ptr, size_t size);

void kfree(void* ptr);

u64 kmalloc_virt_to_phys(void* virt_ptr);
u64 kmalloc_get_kernel_heap_base(void);

void kmalloc_debug_info(void);

#endif // KMALLOC_H