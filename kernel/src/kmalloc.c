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
    
    // TODO: dlmalloc hangs with large heaps - temporarily use simple allocator
    // Initialize simple free list for now
    g_free_list = (free_block_t*)heap_info->base_virt;
    g_free_list->size = heap_info->total_size;
    g_free_list->next = NULL;
    
    vga_printf("kmalloc_init: Dynamic allocator ready (temporary simple allocator)\n");
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
    
    *(size_t*)block = aligned_size;
    heap_info->used_size += aligned_size;
    
    return (void*)((char*)block + sizeof(size_t));
}


void kfree(void* ptr) {
    kheap_info_t* heap_info = kheap_get_info();
    if (!ptr || !heap_info || !heap_info->initialized) {
        return;
    }
    
    char* block_start = (char*)ptr - sizeof(size_t);
    size_t block_size = *(size_t*)block_start;
    
    if ((uintptr_t)block_start < (uintptr_t)heap_info->base_virt ||
        (uintptr_t)block_start >= (uintptr_t)heap_info->base_virt + heap_info->total_size) {
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
    uintptr_t heap_base = (uintptr_t)heap_info->base_virt;
    
    if (virt_addr >= heap_base && virt_addr < heap_base + heap_info->total_size) {
        u64 offset = virt_addr - heap_base;
        return heap_info->base_phys + offset;
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
    
    vga_printf("=== Kernel Heap Info (dlmalloc) ===\n");
    vga_printf("Base: 0x%llx, Total Size: %llu KB\n", 
              (u64)heap_info->base_virt, heap_info->total_size / 1024);
    
    if (kernel_mspace) {
        vga_printf("dlmalloc memory space initialized\n");
    } else {
        vga_printf("dlmalloc memory space NOT initialized\n");
    }
}