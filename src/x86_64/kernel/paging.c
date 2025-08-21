#include <stdint.h>
#include <stddef.h>

#include <paging.h>

extern uint64_t page_table_l4[]; // defined in boot32.asm

static inline void invlpg(void* m) {
    asm volatile("invlpg (%0)" ::"r"(m) : "memory");
}

void map_page(void* virt, void* phys, uint64_t flags) {
    uint64_t vaddr = (uint64_t)virt;
    uint64_t paddr = (uint64_t)phys;

    // Walk L4 -> L3 -> L2
    uint64_t l4_index = (vaddr >> 39) & 0x1FF;
    uint64_t l3_index = (vaddr >> 30) & 0x1FF;
    uint64_t l2_index = (vaddr >> 21) & 0x1FF;

    uint64_t* l3 = (uint64_t*)(page_table_l4[l4_index] & ~0xFFFULL);
    if (!l3) return;
    uint64_t* l2 = (uint64_t*)(l3[l3_index] & ~0xFFFULL);
    if (!l2) return;

    // Map using 2MiB huge page
    l2[l2_index] = (paddr & ~0x1FFFFFULL) | flags | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;

    invlpg(virt);
}

void unmap_page(void* virt) {
    uint64_t vaddr = (uint64_t)virt;

    uint64_t l4_index = (vaddr >> 39) & 0x1FF;
    uint64_t l3_index = (vaddr >> 30) & 0x1FF;
    uint64_t l2_index = (vaddr >> 21) & 0x1FF;

    uint64_t* l3 = (uint64_t*)(page_table_l4[l4_index] & ~0xFFFULL);
    if (!l3) return;
    uint64_t* l2 = (uint64_t*)(l3[l3_index] & ~0xFFFULL);
    if (!l2) return;

    l2[l2_index] = 0; // clear entry
    invlpg(virt);
}

