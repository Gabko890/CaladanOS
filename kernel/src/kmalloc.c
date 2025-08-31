#include <kmalloc.h>
#include <cldtypes.h>
#include <ldinfo.h>
#include <memory_mapper.h>
#include <vgaio.h>
#include <string.h>

static heap_info_t kernel_heap = {0};
static struct memory_info *global_minfo = NULL;

#define MIN_BLOCK_SIZE sizeof(free_block_t)
#define ALIGNMENT 16

static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static void* align_ptr(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    return (void*)((addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1));
}

static void insert_free_block(heap_info_t* heap, free_block_t* block);
static void merge_free_blocks(heap_info_t* heap);

static free_block_t* find_free_block(heap_info_t* heap, size_t size) {
    free_block_t* current = heap->free_list;
    free_block_t* prev = NULL;

    while (current) {
        if (current->size >= size) {
            if (prev) {
                prev->next = current->next;
            } else {
                heap->free_list = current->next;
            }
            return current;
        }
        prev = current;
        current = current->next;
    }
    return NULL;
}

static void split_block(heap_info_t* heap, free_block_t* block, size_t size) {
    if (block->size > size + MIN_BLOCK_SIZE) {
        free_block_t* new_block = (free_block_t*)((char*)block + size);
        new_block->size = block->size - size;
        new_block->next = NULL;
        
        block->size = size;
        
        // Add the new block back to the free list
        insert_free_block(heap, new_block);
    }
}

static void merge_free_blocks(heap_info_t* heap) {
    free_block_t* current = heap->free_list;
    
    while (current && current->next) {
        char* current_end = (char*)current + current->size;
        if (current_end == (char*)current->next) {
            current->size += current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

static void insert_free_block(heap_info_t* heap, free_block_t* block) {
    if (!heap->free_list || (uintptr_t)block < (uintptr_t)heap->free_list) {
        block->next = heap->free_list;
        heap->free_list = block;
    } else {
        free_block_t* current = heap->free_list;
        while (current->next && (uintptr_t)current->next < (uintptr_t)block) {
            current = current->next;
        }
        block->next = current->next;
        current->next = block;
    }
    merge_free_blocks(heap);
}

static u64 find_suitable_memory_region(struct memory_info* minfo, size_t required_size) {
    u64 kernel_end_phys = (u64)__kernel_end_lma;
    u64 page_table_end = 0x1000000ULL; // Estimate 16MB for page tables (conservative)
    
    vga_printf("Looking for %llu bytes of memory\n", required_size);
    vga_printf("Kernel ends at: 0x%llx\n", kernel_end_phys);
    vga_printf("Page table area ends at (est): 0x%llx\n", page_table_end);
    
    for (size_t i = 0; i < minfo->count; i++) {
        struct memory_region* r = &minfo->regions[i];
        vga_printf("Region %zu: 0x%llx-0x%llx (size=%lluMB) flags=0x%x\n", 
                  i, r->addr_start, r->addr_end, r->size >> 20, r->flags);
        
        if (r->flags & MEMORY_INFO_SYSTEM_RAM && r->size >= required_size) {
            // Start after kernel, page tables, and other system areas
            u64 safe_start = kernel_end_phys;
            if (safe_start < page_table_end) {
                safe_start = page_table_end;
            }
            
            // Ensure we're in this region and align to 1MB boundary
            if (safe_start < r->addr_start) {
                safe_start = r->addr_start;
            }
            safe_start = (safe_start + 0xFFFFFULL) & ~0xFFFFFULL;
            
            if (safe_start + required_size <= r->addr_end) {
                vga_printf("Selected address: 0x%llx in region %zu\n", safe_start, i);
                return safe_start;
            }
        }
    }
    vga_printf("No suitable memory region found!\n");
    return 0;
}

void kmalloc_init(struct memory_info* minfo) {
    global_minfo = minfo;
    
    size_t kernel_heap_size = 24ULL << 20;  // 24MB for kernel heap (increased from 8MB)
    
    vga_printf("kmalloc_init: Initializing dynamic allocator\n");
    vga_printf("kmalloc_init: Found %zu memory regions\n", minfo->count);
    
    u64 kernel_heap_phys = find_suitable_memory_region(minfo, kernel_heap_size);
    if (!kernel_heap_phys) {
        vga_printf("kmalloc_init: Failed to find suitable memory region\n");
        return;
    }
    
    // Calculate virtual addresses based on kernel layout to avoid collisions
    u64 kernel_virt_end = (u64)__kernel_end_vma;
    u64 kernel_heap_virt = (kernel_virt_end + 0xFFFFFULL) & ~0xFFFFFULL; // Align to 1MB
    
    kernel_heap.base_virt = (void*)kernel_heap_virt;
    kernel_heap.base_phys = kernel_heap_phys;
    kernel_heap.total_size = kernel_heap_size;
    kernel_heap.used_size = 0;
    
    vga_printf("Kernel heap: virt=0x%llx phys=0x%llx size=%lluMB\n", 
              (u64)kernel_heap.base_virt, kernel_heap.base_phys, kernel_heap_size >> 20);
    
    for (u64 offset = 0; offset < kernel_heap_size; offset += PAGE_4K) {
        if (!mm_map((u64)kernel_heap.base_virt + offset, 
                   kernel_heap.base_phys + offset, 
                   PTE_RW | PTE_PRESENT, PAGE_4K)) {
            vga_printf("kmalloc_init: Failed to map kernel heap at offset 0x%llx\n", offset);
            return;
        }
    }
    
    kernel_heap.free_list = (free_block_t*)kernel_heap.base_virt;
    kernel_heap.free_list->size = kernel_heap_size;
    kernel_heap.free_list->next = NULL;
    
    kernel_heap.initialized = 1;
    
    vga_printf("kmalloc_init: Dynamic allocator ready\n");
}

void* kmalloc(size_t size) {
    if (!kernel_heap.initialized || size == 0) {
        return NULL;
    }
    
    size_t aligned_size = align_size(size + sizeof(size_t));
    if (aligned_size < MIN_BLOCK_SIZE) {
        aligned_size = MIN_BLOCK_SIZE;
    }
    
    free_block_t* block = find_free_block(&kernel_heap, aligned_size);
    if (!block) {
        return NULL;
    }
    
    split_block(&kernel_heap, block, aligned_size);
    
    *(size_t*)block = aligned_size;
    kernel_heap.used_size += aligned_size;
    
    return (void*)((char*)block + sizeof(size_t));
}


void kfree(void* ptr) {
    if (!ptr || !kernel_heap.initialized) {
        return;
    }
    
    char* block_start = (char*)ptr - sizeof(size_t);
    size_t block_size = *(size_t*)block_start;
    
    if ((uintptr_t)block_start < (uintptr_t)kernel_heap.base_virt ||
        (uintptr_t)block_start >= (uintptr_t)kernel_heap.base_virt + kernel_heap.total_size) {
        return; // Not from our heap
    }
    
    free_block_t* free_block = (free_block_t*)block_start;
    free_block->size = block_size;
    
    kernel_heap.used_size -= block_size;
    insert_free_block(&kernel_heap, free_block);
}


void* krealloc(void* ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }
    
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    char* block_start = (char*)ptr - sizeof(size_t);
    size_t old_size = *(size_t*)block_start;
    size_t old_data_size = old_size - sizeof(size_t);
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }
    
    size_t copy_size = (size < old_data_size) ? size : old_data_size;
    memcpy(new_ptr, ptr, copy_size);
    
    kfree(ptr);
    return new_ptr;
}


u64 kmalloc_virt_to_phys(void* virt_ptr) {
    if (!virt_ptr || !kernel_heap.initialized) {
        return 0;
    }
    
    uintptr_t virt_addr = (uintptr_t)virt_ptr;
    uintptr_t heap_base = (uintptr_t)kernel_heap.base_virt;
    
    if (virt_addr >= heap_base && virt_addr < heap_base + kernel_heap.total_size) {
        u64 offset = virt_addr - heap_base;
        return kernel_heap.base_phys + offset;
    }
    
    return 0;
}


u64 kmalloc_get_kernel_heap_base(void) {
    return (u64)kernel_heap.base_virt;
}


void kmalloc_debug_info(void) {
    vga_printf("=== Kernel Heap Info ===\n");
    vga_printf("Base: 0x%llx, Size: %llu KB, Used: %llu KB\n", 
              (u64)kernel_heap.base_virt, kernel_heap.total_size / 1024, kernel_heap.used_size / 1024);
}