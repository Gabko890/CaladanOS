#include <kheap.h>
#include <memory_mapper.h>
#include <vgaio.h>
#include <ldinfo.h>

static kheap_info_t kernel_heap = {0};

static inline u64 align_up_u64(u64 v, u64 a) {
    return (v + (a - 1)) & ~(a - 1);
}

size_t kheap_compute_required_size(struct memory_info* minfo) {
    if (!minfo) return 8ULL << 20; // 8MB default
    
    // Calculate total available system RAM
    u64 total_ram = 0ULL;
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        const struct memory_region *r = &minfo->regions[i];
        if (r->flags & MEMORY_INFO_SYSTEM_RAM) {
            total_ram += r->size;
        }
    }
    
    if (total_ram == 0ULL) {
        vga_printf("kheap: No system RAM found, using default 8MB\n");
        return 8ULL << 20;
    }
    
    // Use 1/10th of total RAM for kernel heap
    u64 heap_size = total_ram / 10ULL;
    
    // Set reasonable bounds
    const u64 MIN_HEAP = 8ULL << 20;   // 8MB minimum
    const u64 MAX_HEAP = 32ULL << 20; // 32MB maximum (reduced for dlmalloc compatibility)
    
    if (heap_size < MIN_HEAP) heap_size = MIN_HEAP;
    if (heap_size > MAX_HEAP) heap_size = MAX_HEAP;
    
    // Align to 1MB boundary for better management
    heap_size = align_up_u64(heap_size, 1ULL << 20);
    
    vga_printf("kheap: Total RAM: %llu MB, Allocating %llu MB (%llu%%) for kernel heap\n",
              total_ram >> 20, heap_size >> 20, (heap_size * 100) / total_ram);
    
    return (size_t)heap_size;
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

u8 kheap_init(struct memory_info* minfo) {
    if (kernel_heap.initialized) {
        vga_printf("kheap: Already initialized\n");
        return 0;
    }
    
    if (!minfo) {
        vga_printf("kheap: Invalid memory info\n");
        return 0;
    }
    
    // Calculate required heap size (1/10th of RAM)
    size_t heap_size = kheap_compute_required_size(minfo);
    
    // Reserve physical memory for heap
    u64 heap_phys = kheap_reserve_from_memory_info(minfo, heap_size);
    if (!heap_phys) {
        vga_printf("kheap: Failed to reserve physical memory\n");
        return 0;
    }
    
    // Calculate virtual address (after kernel with proper alignment)
    u64 kernel_end_aligned = ((u64)__kernel_end_vma + 0xFFFFFULL) & ~0xFFFFFULL; // 1MB align
    u64 heap_virt = kernel_end_aligned + 0x100000ULL; // 1MB gap after kernel
    
    // Map the heap to virtual space
    for (u64 offset = 0; offset < heap_size; offset += PAGE_4K) {
        if (!mm_map(heap_virt + offset, heap_phys + offset, PTE_RW | PTE_PRESENT, PAGE_4K)) {
            vga_printf("kheap: Failed to map heap at offset 0x%llx\n", offset);
            return 0;
        }
    }
    
    // Initialize heap structure
    kernel_heap.base_virt = (void*)heap_virt;
    kernel_heap.base_phys = heap_phys;
    kernel_heap.total_size = heap_size;
    kernel_heap.used_size = 0;
    kernel_heap.initialized = 1;
    
    vga_printf("kheap: Initialized at virt=0x%llx phys=0x%llx size=%llu MB\n",
              heap_virt, heap_phys, heap_size >> 20);
    
    return 1;
}

kheap_info_t* kheap_get_info(void) {
    return kernel_heap.initialized ? &kernel_heap : NULL;
}