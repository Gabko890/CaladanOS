#include <kheap.h>
#include <memory_mapper.h>
#include <vgaio.h>
#include <ldinfo.h>

#define KHEAP_VIRT_BASE 0xFFFFA00000000000ULL

static kheap_info_t kernel_heap = {0};

static inline u64 align_up_u64(u64 v, u64 a) {
    return (v + (a - 1)) & ~(a - 1);
}

static inline u64 align_down_u64(u64 v, u64 a) {
    return v & ~(a - 1);
}

size_t kheap_compute_required_size(struct memory_info* minfo) {
    if (!minfo) return 0;

    u64 total_ram = 0ULL;
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        const struct memory_region *r = &minfo->regions[i];
        if (!(r->flags & MEMORY_INFO_SYSTEM_RAM) || r->size == 0) continue;

        u64 start = align_up_u64(r->addr_start, PAGE_4K);
        u64 end = align_down_u64(r->addr_end + 1ULL, PAGE_4K);
        if (end > start) total_ram += end - start;
    }

    return (size_t)total_ram;
}

u64 kheap_reserve_from_memory_info(struct memory_info* minfo, size_t bytes_required) {
    if (!minfo || bytes_required == 0) return 0;
    
    const u64 align = 1ULL << 20; // 1MB alignment for heap
    u64 kernel_end_phys = (u64)__kernel_end_lma;
    
    vga_printf("kheap: Looking for %llu MB of memory after kernel (ends at 0x%llx)\n", 
              bytes_required >> 20, kernel_end_phys);
    
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        struct memory_region *r = &minfo->regions[i];
        if (!(r->flags & MEMORY_INFO_SYSTEM_RAM)) continue;
        
        vga_printf("kheap: Checking region %zu: 0x%llx-0x%llx (%llu MB)\n", 
                  i, r->addr_start, r->addr_end, r->size >> 20);
        
        // Start after kernel and align
        u64 candidate_start = r->addr_start;
        if (candidate_start < kernel_end_phys) {
            candidate_start = kernel_end_phys;
        }
        candidate_start = align_up_u64(candidate_start, align);
        
        // Check if this region has enough space
        if (candidate_start >= r->addr_end) continue;
        u64 available = r->addr_end - candidate_start;
        if (available < (u64)bytes_required) continue;
        
        // Reserve the memory by splitting the region
        u64 reserved_start = candidate_start;
        u64 new_region_start = candidate_start + (u64)bytes_required;
        u64 new_region_size = r->addr_end - new_region_start;
        
        r->addr_start = new_region_start;
        r->size = new_region_size;
        r->addr_end = (r->size == 0) ? r->addr_start : (r->addr_start + r->size - 1);
        
        vga_printf("kheap: Reserved 0x%llx-0x%llx (%llu MB) from region %zu\n",
                  reserved_start, reserved_start + bytes_required - 1, bytes_required >> 20, i);
        
        return reserved_start;
    }
    
    vga_printf("kheap: No suitable memory region found for %llu MB heap\n", bytes_required >> 20);
    return 0;
}

static u8 kheap_map_range(u64 virt, u64 phys, u64 size) {
    u64 off = 0;
    while (off < size) {
        u64 v = virt + off;
        u64 p = phys + off;
        u64 remaining = size - off;

        if ((v & (PAGE_2M - 1ULL)) == 0 &&
            (p & (PAGE_2M - 1ULL)) == 0 &&
            remaining >= PAGE_2M) {
            if (!mm_map(v, p, PTE_RW | PTE_PRESENT | PTE_HUGE, PAGE_2M)) {
                vga_printf("kheap: Failed to map 2M heap page virt=0x%llx phys=0x%llx\n", v, p);
                return 0;
            }
            off += PAGE_2M;
        } else {
            if (!mm_map(v, p, PTE_RW | PTE_PRESENT, PAGE_4K)) {
                vga_printf("kheap: Failed to map 4K heap page virt=0x%llx phys=0x%llx\n", v, p);
                return 0;
            }
            off += PAGE_4K;
        }
    }
    return 1;
}

u8 kheap_init(struct memory_info* minfo) {
    if (kernel_heap.initialized) {
        vga_printf("kheap: Already initialized\n");
        return 0;
    }
    
    if (!minfo) {
        vga_printf("kheap: Invalid memory info\n");
        return 0;
    }

    size_t heap_size = kheap_compute_required_size(minfo);
    if (heap_size == 0) {
        vga_printf("kheap: No usable system RAM left for heap\n");
        return 0;
    }

    u64 next_virt = KHEAP_VIRT_BASE;
    size_t segment_count = 0;
    size_t total_mapped = 0;

    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        struct memory_region *r = &minfo->regions[i];
        if (!(r->flags & MEMORY_INFO_SYSTEM_RAM) || r->size == 0) continue;

        u64 phys_start = align_up_u64(r->addr_start, PAGE_4K);
        u64 phys_end = align_down_u64(r->addr_end + 1ULL, PAGE_4K);
        if (phys_end <= phys_start) {
            r->size = 0;
            r->addr_end = r->addr_start;
            continue;
        }

        u64 segment_size = phys_end - phys_start;
        u64 segment_virt = align_up_u64(next_virt, PAGE_2M) + (phys_start & (PAGE_2M - 1ULL));
        if (segment_virt < next_virt) segment_virt += PAGE_2M;

        if (segment_count >= KHEAP_MAX_SEGMENTS) {
            vga_printf("kheap: Too many heap segments\n");
            return 0;
        }

        if (!kheap_map_range(segment_virt, phys_start, segment_size)) return 0;

        kernel_heap.segments[segment_count].base_virt = (void*)segment_virt;
        kernel_heap.segments[segment_count].base_phys = phys_start;
        kernel_heap.segments[segment_count].size = (size_t)segment_size;
        segment_count++;
        total_mapped += (size_t)segment_size;

        next_virt = segment_virt + segment_size;

        // This RAM is now owned by the allocator.
        r->addr_start = phys_end;
        r->addr_end = phys_end;
        r->size = 0;
    }

    if (segment_count == 0 || total_mapped == 0) {
        vga_printf("kheap: No heap segments mapped\n");
        return 0;
    }

    // Initialize heap structure
    kernel_heap.base_virt = kernel_heap.segments[0].base_virt;
    kernel_heap.base_phys = kernel_heap.segments[0].base_phys;
    kernel_heap.total_size = total_mapped;
    kernel_heap.used_size = 0;
    kernel_heap.segment_count = segment_count;
    kernel_heap.initialized = 1;
    
    vga_printf("kheap: Initialized size=%llu MB using %llu RAM segments\n",
              (u64)kernel_heap.total_size >> 20, (u64)segment_count);
    
    return 1;
}

kheap_info_t* kheap_get_info(void) {
    return kernel_heap.initialized ? &kernel_heap : NULL;
}
