#ifndef KHEAP_H
#define KHEAP_H

#include <cldtypes.h>
#include <memory_info.h>

#define KHEAP_MAX_SEGMENTS MEMORY_INFO_MAX

typedef struct kheap_segment {
    void* base_virt;
    u64 base_phys;
    size_t size;
} kheap_segment_t;

typedef struct kheap_info {
    void* base_virt;
    u64 base_phys;
    size_t total_size;
    size_t used_size;
    size_t segment_count;
    kheap_segment_t segments[KHEAP_MAX_SEGMENTS];
    u8 initialized;
} kheap_info_t;

// Initialize kernel heap with dynamic sizing based on available memory
u8 kheap_init(struct memory_info* minfo);

// Get kernel heap information
kheap_info_t* kheap_get_info(void);

// Return total remaining system RAM that can be mapped into the kernel heap.
size_t kheap_compute_required_size(struct memory_info* minfo);

// Legacy helper: reserve a single physical region from memory info.
u64 kheap_reserve_from_memory_info(struct memory_info* minfo, size_t bytes_required);

#endif // KHEAP_H
