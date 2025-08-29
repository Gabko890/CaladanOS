#ifndef SIMPLE_MALLOC_H
#define SIMPLE_MALLOC_H

#include <cldtypes.h>
#include <memory_info.h>

void* simple_malloc(size_t size);
void simple_free(void* ptr);
void simple_malloc_init(void);

void kmalloc_init(struct memory_info* minfo);
void* kmalloc(size_t size);
void* kmalloc_userland(size_t size);
void kfree(void* ptr);
void kfree_userland(void* ptr);

u64 kmalloc_virt_to_phys(void* virt_ptr);
u64 kmalloc_userland_virt_to_phys(void* virt_ptr);
u64 kmalloc_get_kernel_heap_base(void);
u64 kmalloc_get_userland_heap_base(void);

#endif // SIMPLE_MALLOC_H