#include <kmalloc.h>
#include <kheap.h>
#include <cldtypes.h>
#include <vgaio.h>
#include <string.h>
#include <dlmalloc/malloc.h>

// dlmalloc memory space for kernel heap
static mspace kernel_mspace = 0;

// Simple allocator (temporary fallback)
static free_block_t* g_free_list = NULL;

#define MIN_BLOCK_SIZE sizeof(free_block_t)
#define ALIGNMENT 16

static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

// Forward declarations
static void insert_free_block(free_block_t* block);
static void merge_free_blocks(void);
static int ptr_in_heap_segment(kheap_info_t* heap_info, uintptr_t addr, size_t size);

static free_block_t* find_free_block(size_t size) {
    free_block_t* current = g_free_list;
    free_block_t* prev = NULL;

    while (current) {
        if (current->size >= size) {
            if (prev) {
                prev->next = current->next;
            } else {
                g_free_list = current->next;
            }
            return current;
        }
        prev = current;
        current = current->next;
    }
    return NULL;
}

static void split_block(free_block_t* block, size_t size) {
    if (block->size > size + MIN_BLOCK_SIZE) {
        free_block_t* new_block = (free_block_t*)((char*)block + size);
        new_block->size = block->size - size;
        new_block->next = NULL;
        
        block->size = size;
        
        insert_free_block(new_block);
    }
}

static void merge_free_blocks(void) {
    free_block_t* current = g_free_list;
    
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

static void insert_free_block(free_block_t* block) {
    if (!g_free_list || (uintptr_t)block < (uintptr_t)g_free_list) {
        block->next = g_free_list;
        g_free_list = block;
    } else {
        free_block_t* current = g_free_list;
        while (current->next && (uintptr_t)current->next < (uintptr_t)block) {
            current = current->next;
        }
        block->next = current->next;
        current->next = block;
    }
    merge_free_blocks();
}


void kmalloc_init(struct memory_info* minfo) {
    vga_printf("kmalloc_init: Initializing dynamic allocator\n");
    
    // Initialize kernel heap using new kheap module
    if (!kheap_init(minfo)) {
        vga_printf("kmalloc_init: Failed to initialize kernel heap\n");
        return;
    }
    
    // Get heap info
    kheap_info_t* heap_info = kheap_get_info();
    if (!heap_info) {
        vga_printf("kmalloc_init: Failed to get heap info\n");
        return;
    }
    
    g_free_list = NULL;
    for (size_t i = 0; i < heap_info->segment_count; i++) {
        if (heap_info->segments[i].size < MIN_BLOCK_SIZE) continue;
        free_block_t* block = (free_block_t*)heap_info->segments[i].base_virt;
        block->size = heap_info->segments[i].size;
        block->next = NULL;
        insert_free_block(block);
    }
    
    vga_printf("kmalloc_init: Dynamic allocator ready (%llu KB available)\n",
              (u64)heap_info->total_size / 1024);
}

void* kmalloc(size_t size) {
    kheap_info_t* heap_info = kheap_get_info();
    if (!heap_info || !heap_info->initialized || size == 0) {
        return NULL;
    }
    
    size_t aligned_size = align_size(size + sizeof(size_t));
    if (aligned_size < MIN_BLOCK_SIZE) {
        aligned_size = MIN_BLOCK_SIZE;
    }
    
    free_block_t* block = find_free_block(aligned_size);
    if (!block) {
        return NULL;
    }
    
    split_block(block, aligned_size);
    
    *(size_t*)block = block->size;
    heap_info->used_size += block->size;
    
    return (void*)((char*)block + sizeof(size_t));
}


static int ptr_in_heap_segment(kheap_info_t* heap_info, uintptr_t addr, size_t size) {
    if (!heap_info || size == 0) return 0;
    if (addr + size < addr) return 0;

    for (size_t i = 0; i < heap_info->segment_count; i++) {
        uintptr_t seg_base = (uintptr_t)heap_info->segments[i].base_virt;
        uintptr_t seg_end = seg_base + heap_info->segments[i].size;
        if (addr >= seg_base && addr + size <= seg_end) return 1;
    }
    return 0;
}

void kfree(void* ptr) {
    kheap_info_t* heap_info = kheap_get_info();
    if (!ptr || !heap_info || !heap_info->initialized) {
        return;
    }
    
    char* block_start = (char*)ptr - sizeof(size_t);
    size_t block_size = *(size_t*)block_start;
    
    if (!ptr_in_heap_segment(heap_info, (uintptr_t)block_start, block_size)) {
        return;
    }
    
    free_block_t* free_block = (free_block_t*)block_start;
    free_block->size = block_size;
    
    heap_info->used_size -= block_size;
    insert_free_block(free_block);
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
    kheap_info_t* heap_info = kheap_get_info();
    if (!virt_ptr || !heap_info || !heap_info->initialized) {
        return 0;
    }
    
    uintptr_t virt_addr = (uintptr_t)virt_ptr;

    for (size_t i = 0; i < heap_info->segment_count; i++) {
        uintptr_t seg_base = (uintptr_t)heap_info->segments[i].base_virt;
        uintptr_t seg_end = seg_base + heap_info->segments[i].size;
        if (virt_addr >= seg_base && virt_addr < seg_end) {
            u64 offset = virt_addr - seg_base;
            return heap_info->segments[i].base_phys + offset;
        }
    }
    
    return 0;
}


u64 kmalloc_get_kernel_heap_base(void) {
    kheap_info_t* heap_info = kheap_get_info();
    return heap_info ? (u64)heap_info->base_virt : 0;
}


void kmalloc_debug_info(void) {
    kheap_info_t* heap_info = kheap_get_info();
    if (!heap_info) {
        vga_printf("=== Kernel Heap Not Initialized ===\n");
        return;
    }
    
    vga_printf("=== Kernel Heap Info ===\n");
    vga_printf("Base: 0x%llx, Total Size: %llu KB, Used: %llu KB, Segments: %llu\n",
              (u64)heap_info->base_virt,
              heap_info->total_size / 1024,
              heap_info->used_size / 1024,
              (u64)heap_info->segment_count);

    for (size_t i = 0; i < heap_info->segment_count; i++) {
        vga_printf("  segment %llu: virt=0x%llx phys=0x%llx size=%llu KB\n",
                  (u64)i,
                  (u64)heap_info->segments[i].base_virt,
                  heap_info->segments[i].base_phys,
                  heap_info->segments[i].size / 1024);
    }
    
    if (kernel_mspace) {
        vga_printf("dlmalloc memory space initialized\n");
    } else {
        vga_printf("dlmalloc memory space NOT initialized\n");
    }
}
