#include "memory_mapper.h"
#include "cldtypes.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t pte_t;

#define IDX_PML4(va)  (((va) >> 39) & 0x1FF)
#define IDX_PDPT(va)  (((va) >> 30) & 0x1FF)
#define IDX_PD(va)    (((va) >> 21) & 0x1FF)
#define IDX_PT(va)    (((va) >> 12) & 0x1FF)

enum { _PAGE_SIZE_4K = 4096, _PAGE_SIZE_2M = 2 * 1024 * 1024 };

static struct {
    u64 table_phys_base;
    void *table_virt_base;
    u64 table_virt_base_pending; // Stored for later use
    size_t table_size;
    size_t next_free;
    pte_t *pml4;
    u8 initialized;
} mm = {0};

static inline void *phys_to_virt(u64 phys) {
    if (!mm.table_virt_base) return (void *)(uintptr_t)phys;
    return (void *)((uintptr_t)mm.table_virt_base + (phys - mm.table_phys_base));
}
static inline u64 virt_to_phys(void *vptr) {
    if (!mm.table_virt_base) return (u64)(uintptr_t)vptr;
    return mm.table_phys_base + ((uintptr_t)vptr - (uintptr_t)mm.table_virt_base);
}

static pte_t *alloc_table_page(void) {
    if (!mm.initialized) return NULL;
    size_t off = (mm.next_free + (_PAGE_SIZE_4K - 1)) & ~((size_t)(_PAGE_SIZE_4K - 1));
    if (off + _PAGE_SIZE_4K > mm.table_size) return NULL;
    void *v = phys_to_virt(mm.table_phys_base + off);
    memset(v, 0, _PAGE_SIZE_4K);
    mm.next_free = off + _PAGE_SIZE_4K;
    return (pte_t *)v;
}

static pte_t *get_or_alloc_next_table(pte_t *parent_table, unsigned idx) {
    pte_t entry = parent_table[idx];
    if (entry & PTE_PRESENT) {
        u64 child_phys = entry & 0x000FFFFFFFFFF000ULL;
        return (pte_t *)phys_to_virt(child_phys);
    }
    pte_t *child = alloc_table_page();
    if (!child) return NULL;
    u64 child_phys = virt_to_phys(child) & 0x000FFFFFFFFFF000ULL;
    parent_table[idx] = (pte_t)(child_phys | PTE_PRESENT | PTE_RW);
    return child;
}

static inline u64 align_up_u64(u64 v, u64 a) {
    return (v + (a - 1)) & ~(a - 1);
}

static size_t compute_required_table_bytes(const struct memory_info *minfo) {
    if (!minfo) return 32 * 1024 * 1024;
    
    unsigned long long total_ram = 0ULL;
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        const struct memory_region *r = &minfo->regions[i];
        if (r->flags & MEMORY_INFO_SYSTEM_RAM) total_ram += r->size;
    }
    if (total_ram == 0ULL) return 32 * 1024 * 1024;
    
    // Calculate page table requirements:
    // For identity mapping of total RAM + kernel mappings + extra mappings
    // Assume we might need to map up to 2x total RAM for flexibility
    unsigned long long mappable_space = total_ram * 2;
    
    // Calculate worst-case page table pages needed:
    // - 1 PML4 (covers 512GB each entry)
    // - PDPTs: mappable_space / (1GB) entries needed
    // - PDs: mappable_space / (2MB) entries for 2MB pages
    // Each table is 4KB, with 512 entries
    
    unsigned long long gb_512 = 512ULL * 1024ULL * 1024ULL * 1024ULL; // 512GB
    unsigned long long gb_1 = 1024ULL * 1024ULL * 1024ULL; // 1GB
    unsigned long long mb_2 = 2ULL * 1024ULL * 1024ULL; // 2MB
    
    // Number of page tables needed
    unsigned long long pml4_count = 1;
    unsigned long long pdpt_count = (mappable_space + gb_512 - 1) / gb_512;
    unsigned long long pd_count = (mappable_space + gb_1 - 1) / gb_1;
    
    // Total page table pages * 4KB per page
    unsigned long long table_pages = pml4_count + pdpt_count + pd_count;
    unsigned long long reserve = table_pages * 4096ULL;
    
    // Set reasonable bounds based on system size
    const unsigned long long MIN_BYTES = 8ULL * 1024ULL * 1024ULL;   // 8MB minimum
    const unsigned long long MAX_BYTES = 128ULL * 1024ULL * 1024ULL; // 128MB maximum
    
    if (reserve < MIN_BYTES) reserve = MIN_BYTES;
    if (reserve > MAX_BYTES) reserve = MAX_BYTES;
    
    // Align to page boundary
    reserve = (reserve + 0xFFFULL) & ~0xFFFULL;
    return (size_t)reserve;
}

static u64 reserve_from_memory_info(struct memory_info *minfo, size_t bytes_required) {
    if (!minfo || bytes_required == 0) return 0;
    const u64 align = 0x1000ULL;
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        struct memory_region *r = &minfo->regions[i];
        if (!(r->flags & MEMORY_INFO_SYSTEM_RAM)) continue;
        u64 candidate_start = align_up_u64(r->addr_start, align);
        u64 waste = candidate_start - r->addr_start;
        if (r->size < waste) continue;
        u64 avail_after_align = r->size - waste;
        if (avail_after_align < (u64)bytes_required) continue;
        u64 reserved_start = candidate_start;
        u64 new_region_start = candidate_start + (u64)bytes_required;
        u64 new_region_size = r->size - waste - (u64)bytes_required;
        r->addr_start = new_region_start;
        r->size = new_region_size;
        r->addr_end = (r->size == 0) ? 0 : (r->addr_start + r->size - 1);
        return reserved_start;
    }
    return 0;
}

u64 mm_init(struct memory_info* minfo, u64 table_virt_base) {
    if (!minfo) return 0;
    if (mm.initialized) return 0;
    size_t required = compute_required_table_bytes(minfo);
    if (required == 0) return 0;
    u64 reserved_phys = reserve_from_memory_info(minfo, required);
    if (reserved_phys == 0) return 0;
    mm.table_phys_base = reserved_phys;
    mm.table_size = required;
    mm.next_free = 0;
    mm.pml4 = NULL;
    mm.table_virt_base = NULL;
    mm.initialized = 1;
    pte_t *p = alloc_table_page();
    if (!p) {
        mm.initialized = 0;
        return 0;
    }
    mm.pml4 = p;
    
    // Calculate PML4 physical address while still using physical addressing
    u64 pml4_phys = virt_to_phys((void *)mm.pml4) & 0x000FFFFFFFFFF000ULL;

    // Store the table virtual base for later use but keep using physical for now
    mm.table_virt_base_pending = table_virt_base;
    mm.table_virt_base = NULL; // Use physical addressing until CR3 switch

    return pml4_phys; // Return PML4 physical address for CR3 switch
}

// New function to enable virtual page table access after CR3 switch and mappings are set up
u8 mm_enable_virtual_tables(void) {
    if (!mm.initialized || !mm.table_virt_base_pending) return 0;
    
    // Map the entire page table area to virtual space
    for (u64 offset = 0; offset < mm.table_size; offset += PAGE_4K) {
        if (!mm_map(mm.table_virt_base_pending + offset, mm.table_phys_base + offset, PTE_PRESENT | PTE_RW, PAGE_4K)) {
            return 0;
        }
    }
    
    // Now switch to virtual addressing
    mm.table_virt_base = (void*)mm.table_virt_base_pending;
    
    return 1; // Success
}

u8 mm_map(u64 virtual_addr, u64 physical_addr, u64 flags, size_t page_size) {
    if (!mm.initialized) return 0;
    if (!is_canonical(virtual_addr) || !is_canonical(physical_addr)) return 0;
    u8 huge = (flags & PTE_HUGE) ? 1 : 0;
    if (huge) page_size = PAGE_2M;
    if (page_size != PAGE_4K && page_size != PAGE_2M) return 0;
    if (!is_aligned(virtual_addr, page_size) || !is_aligned(physical_addr, page_size)) return 0;
    pte_t *pml4 = mm.pml4;
    if (!pml4) return 0;
    unsigned i_pml4 = IDX_PML4(virtual_addr);
    unsigned i_pdpt = IDX_PDPT(virtual_addr);
    unsigned i_pd   = IDX_PD(virtual_addr);
    unsigned i_pt   = IDX_PT(virtual_addr);
    pte_t *pdpt = get_or_alloc_next_table(pml4, i_pml4);
    if (!pdpt) return 0;
    pte_t *pd = get_or_alloc_next_table(pdpt, i_pdpt);
    if (!pd) return 0;
    if (page_size == PAGE_2M) {
        u64 paddr_field = physical_addr & 0x000FFFFFFFFFF000ULL;
        pte_t entry = (pte_t)(paddr_field | (flags & ~(PTE_HUGE)) | PTE_PRESENT | PTE_HUGE);
        if (flags & PTE_NX) entry |= PTE_NX;
        pd[i_pd] = entry;
        return 1;
    } else {
        pte_t *pt = get_or_alloc_next_table(pd, i_pd);
        if (!pt) return 0;
        u64 paddr_field = physical_addr & 0x000FFFFFFFFFF000ULL;
        pte_t entry = (pte_t)(paddr_field | (flags & ~(PTE_HUGE)) | PTE_PRESENT);
        if (flags & PTE_NX) entry |= PTE_NX;
        pt[i_pt] = entry;
        return 1;
    }
}

u8 mm_unmap(u64 virtual_addr, size_t page_size) {
    if (!mm.initialized) return 0;
    if (!is_canonical(virtual_addr)) return 0;
    if (page_size != PAGE_4K && page_size != PAGE_2M) return 0;
    if (!is_aligned(virtual_addr, page_size)) return 0;
    pte_t *pml4 = mm.pml4;
    if (!pml4) return 0;
    unsigned i_pml4 = IDX_PML4(virtual_addr);
    unsigned i_pdpt = IDX_PDPT(virtual_addr);
    unsigned i_pd   = IDX_PD(virtual_addr);
    unsigned i_pt   = IDX_PT(virtual_addr);
    pte_t e_pml4 = pml4[i_pml4];
    if (!(e_pml4 & PTE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)phys_to_virt(e_pml4 & 0x000FFFFFFFFFF000ULL);
    pte_t e_pdpt = pdpt[i_pdpt];
    if (!(e_pdpt & PTE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)phys_to_virt(e_pdpt & 0x000FFFFFFFFFF000ULL);
    pte_t e_pd = pd[i_pd];
    if (!(e_pd & PTE_PRESENT)) return 0;
    if (page_size == PAGE_2M) {
        pd[i_pd] = 0;
        return 1;
    } else {
        pte_t *pt = (pte_t *)phys_to_virt(e_pd & 0x000FFFFFFFFFF000ULL);
        pte_t e_pt = pt[i_pt];
        if (!(e_pt & PTE_PRESENT)) return 0;
        pt[i_pt] = 0;
        return 1;
    }
}

