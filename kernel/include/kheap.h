#ifndef KHEAP_H
#define KHEAP_H

#include <cldtypes.h>
#include <memory_info.h>

typedef struct kheap_info {
    void* base_virt;
    u64 base_phys;
    size_t total_size;
    size_t used_size;
    u8 initialized;
} kheap_info_t;

// Initialize kernel heap with dynamic sizing based on available memory
u8 kheap_init(struct memory_info* minfo);

// Get kernel heap information
kheap_info_t* kheap_get_info(void);

// Reserve memory for kernel heap (1/10th of available RAM)
size_t kheap_compute_required_size(struct memory_info* minfo);

// Reserve physical memory region for heap
u64 kheap_reserve_from_memory_info(struct memory_info* minfo, size_t bytes_required);

#endif // KHEAP_H