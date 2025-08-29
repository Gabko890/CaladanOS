#include <simple_malloc.h>
#include <cldtypes.h>
#include <ldinfo.h>
#include <memory_mapper.h>
#include <vgaio.h>

static char *heap_current = NULL;
static char *heap_end = NULL;

static char *kernel_heap_current = NULL;
static char *kernel_heap_end = NULL;
static char *userland_heap_current = NULL;
static char *userland_heap_end = NULL;

static struct memory_info *global_minfo = NULL;
static u64 kernel_heap_phys_base = 0;
static u64 userland_heap_phys_base = 0;
static u64 kernel_heap_virt_base = 0;
static u64 userland_heap_virt_base = 0;

void simple_malloc_init(void) {
    heap_current = (char*)0xFFFFFFFF90000000ULL;  // Virtual heap address
    heap_end = (char*)(0xFFFFFFFF90000000ULL + (4ULL << 20));  // 4MB heap
}

void* simple_malloc(size_t size) {
    if (!heap_current) return NULL;  // Not initialized
    if (size == 0) return NULL;
    size = (size + 15) & ~15ULL;  // 16-byte align
    if (heap_current + size > heap_end) return NULL;  // Out of memory
    char *result = heap_current;
    heap_current += size;
    return result;
}

void simple_free(void* ptr) {
    // Simple allocator doesn't actually free - just a stub
    (void)ptr;
}

void kmalloc_init(struct memory_info* minfo) {
    global_minfo = minfo;
    size_t heap_size = 4ULL << 20;  // 4MB each
    
    vga_printf("kmalloc_init: Found %zu memory regions\n", minfo->count);
    
    // Use safer physical addresses within the lower 4GB range
    // Find a region and use offsets within it
    u64 kernel_heap_phys = 0;
    u64 userland_heap_phys = 0;
    
    for (size_t i = 0; i < minfo->count; i++) {
        struct memory_region *r = &minfo->regions[i];
        vga_printf("Region %zu: 0x%llx-0x%llx size=%llu flags=0x%x\n", 
                  i, r->addr_start, r->addr_end, r->size, r->flags);
        if (r->flags & MEMORY_INFO_SYSTEM_RAM && r->size >= (heap_size * 2)) {
            // Use this region for both heaps, but keep them separate
            // Use a safe area within the first 16MB that's identity mapped
            u64 safe_start = 0x2000000ULL; // 32MB mark - should be identity mapped
            
            kernel_heap_phys = safe_start;
            userland_heap_phys = safe_start + heap_size;
            
            kernel_heap_phys_base = kernel_heap_phys;
            userland_heap_phys_base = userland_heap_phys;
            
            vga_printf("Kernel heap physical: 0x%llx\n", kernel_heap_phys);
            vga_printf("Userland heap physical: 0x%llx\n", userland_heap_phys);
            break;
        }
    }
    
    // Set virtual addresses
    kernel_heap_virt_base = 0xFFFFFFFF91000000ULL;
    userland_heap_virt_base = 0x40000000ULL;
    
    // TODO: Map heaps when memory mapper is more stable
    vga_printf("Heap infrastructure ready (mapping disabled for now)\n");
    
    // Initialize allocators
    kernel_heap_current = (char*)kernel_heap_virt_base;
    kernel_heap_end = (char*)(kernel_heap_virt_base + heap_size);
    userland_heap_current = (char*)userland_heap_virt_base;
    userland_heap_end = (char*)(userland_heap_virt_base + heap_size);
    
    vga_printf("kmalloc_init completed\n");
}

void* kmalloc(size_t size) {
    // For now, fall back to simple_malloc until mapping is working
    return simple_malloc(size);
}

void* kmalloc_userland(size_t size) {
    // For testing, return a fake userland address that we can track
    if (size == 0) return NULL;
    static u64 fake_userland_addr = 0x40000000ULL;
    void* result = (void*)fake_userland_addr;
    fake_userland_addr += ((size + 15) & ~15ULL);
    return result;
}

void kfree(void* ptr) {
    (void)ptr;
}

void kfree_userland(void* ptr) {
    (void)ptr;
}

u64 kmalloc_virt_to_phys(void* virt_ptr) {
    if (!virt_ptr) return 0;
    u64 virt_addr = (u64)virt_ptr;
    
    // Check if it's from simple_malloc (0xffffffff90000000 range)
    if (virt_addr >= 0xFFFFFFFF90000000ULL && virt_addr < 0xFFFFFFFF94000000ULL) {
        u64 offset = virt_addr - 0xFFFFFFFF90000000ULL;
        return 0x01000000ULL + offset;  // Physical heap for simple_malloc
    }
    
    // Check if it's from kmalloc (when properly mapped)
    if (kernel_heap_virt_base && kernel_heap_phys_base && 
        virt_addr >= kernel_heap_virt_base && virt_addr < (kernel_heap_virt_base + (4ULL << 20))) {
        u64 offset = virt_addr - kernel_heap_virt_base;
        return kernel_heap_phys_base + offset;
    }
    
    return 0;
}

u64 kmalloc_userland_virt_to_phys(void* virt_ptr) {
    if (!virt_ptr || !userland_heap_virt_base) return 0;
    u64 virt_addr = (u64)virt_ptr;
    if (virt_addr < userland_heap_virt_base || virt_addr >= (userland_heap_virt_base + 4ULL << 20)) {
        return 0;  // Not in userland heap range
    }
    u64 offset = virt_addr - userland_heap_virt_base;
    return userland_heap_phys_base + offset;
}

u64 kmalloc_get_kernel_heap_base(void) {
    return kernel_heap_virt_base;
}

u64 kmalloc_get_userland_heap_base(void) {
    return userland_heap_virt_base;
}