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
    if (!minfo) return 2 * 1024 * 1024;
    unsigned long long total_ram = 0ULL;
    for (size_t i = 0; i < minfo->count && i < MEMORY_INFO_MAX; ++i) {
        const struct memory_region *r = &minfo->regions[i];
        if (r->flags & MEMORY_INFO_SYSTEM_RAM) total_ram += r->size;
    }
    if (total_ram == 0ULL) return 2 * 1024 * 1024;
    unsigned long long reserve = (total_ram + 511ULL) / 512ULL;
    const unsigned long long MIN_BYTES = 2ULL * 1024ULL * 1024ULL;
    const unsigned long long MAX_BYTES = 16ULL * 1024ULL * 1024ULL;
    if (reserve < MIN_BYTES) reserve = MIN_BYTES;
    if (reserve > MAX_BYTES) reserve = MAX_BYTES;
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

u64 mm_init(struct memory_info* minfo) {
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
    return virt_to_phys((void *)mm.pml4) & 0x000FFFFFFFFFF000ULL;
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

