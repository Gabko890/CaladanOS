#ifndef KMALLOC_H
#define KMALLOC_H

#include <cldtypes.h>
#include <memory_info.h>

typedef struct free_block {
    size_t size;
    struct free_block* next;
} free_block_t;

typedef struct heap_info {
    void* base_virt;
    u64 base_phys;
    size_t total_size;
    size_t used_size;
    free_block_t* free_list;
    u8 initialized;
} heap_info_t;

void kmalloc_init(struct memory_info* minfo);

void* kmalloc(size_t size);
void* kmalloc_userland(size_t size);
void* krealloc(void* ptr, size_t size);
void* krealloc_userland(void* ptr, size_t size);

void kfree(void* ptr);
void kfree_userland(void* ptr);

u64 kmalloc_virt_to_phys(void* virt_ptr);
u64 kmalloc_userland_virt_to_phys(void* virt_ptr);
u64 kmalloc_get_kernel_heap_base(void);
u64 kmalloc_get_userland_heap_base(void);

void kmalloc_debug_info(void);

#endif // KMALLOC_H