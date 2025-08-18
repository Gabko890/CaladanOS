#include <stdint.h>
#include <stddef.h>

#define PTE_PRESENT 0x1ULL
#define PTE_RW      0x2ULL
#define PTE_PS      0x80ULL

/* physical pages reserved for page tables in this example */
#define PML4_PHYS       0x1000ULL
#define PDPT_PHYS       0x2000ULL
#define PD_PHYS         0x3000ULL
#define PT_PHYS         0x4000ULL

/* separate tables for higher-half mapping (avoid collisions) */
#define PDPT_H_PHYS     0x5000ULL
#define PD_H_PHYS       0x6000ULL
#define PT_H_PHYS       0x7000ULL

#define KERNEL_VA 0xFFFF800000000000ULL

void* memset (void *dest, register int val, register size_t len)
{
    register unsigned char *ptr = (unsigned char*)dest;
    while (len-- > 0)
        *ptr++ = val;
    return dest;
}

void setup_page_tables() {
    uint64_t *pml4   = (uint64_t*)PML4_PHYS;
    uint64_t *pdpt   = (uint64_t*)PDPT_PHYS;
    uint64_t *pd     = (uint64_t*)PD_PHYS;
    uint64_t *pt     = (uint64_t*)PT_PHYS;

    uint64_t *pdpt_h = (uint64_t*)PDPT_H_PHYS;
    uint64_t *pd_h   = (uint64_t*)PD_H_PHYS;
    uint64_t *pt_h   = (uint64_t*)PT_H_PHYS;

    /* clear all tables first */
    memset(pml4,0,4096);
    memset(pdpt,0,4096);
    memset(pd,0,4096);
    memset(pt,0,4096);

    memset(pdpt_h,0,4096);
    memset(pd_h,0,4096);
    memset(pt_h,0,4096);

    /********** 1. Identity map first 2 MiB (PD entry, PS=1) **********/
    /* Put a 2 MiB huge page at pd[0] that maps phys 0..2MiB */
    pd[0] = (0x00000000ULL & ~0x1FFFFFULL) | PTE_PRESENT | PTE_RW | PTE_PS;
    pdpt[0] = PD_PHYS | PTE_PRESENT | PTE_RW;
    pml4[0] = PDPT_PHYS | PTE_PRESENT | PTE_RW;

    /********** 2. Higher-half kernel mapping -> use separate tables **********/
    int pml4_index = (KERNEL_VA >> 39) & 0x1FF;
    int pdpt_index = (KERNEL_VA >> 30) & 0x1FF; /* == 0 for 0xFFFF8000... */
    int pd_index   = (KERNEL_VA >> 21) & 0x1FF; /* == 0 */
    int pt_index   = (KERNEL_VA >> 12) & 0x1FF; /* == 0 */

    /* Wire up the higher-half PML4 -> new PDPT */
    pml4[pml4_index] = PDPT_H_PHYS | PTE_PRESENT | PTE_RW;
    /* The higher-half PDPT -> new PD */
    pdpt_h[pdpt_index] = PD_H_PHYS | PTE_PRESENT | PTE_RW;
    /* The higher-half PD entry points to a PT (not a PS huge page) */
    pd_h[pd_index] = PT_H_PHYS | PTE_PRESENT | PTE_RW;

    /* Map first 512 4KiB pages (2 MiB) at the higher-half VA to phys 0..2MiB */
    for(int i = 0; i < 512; ++i) {
        pt_h[i] = ((uint64_t)i * 0x1000ULL) | PTE_PRESENT | PTE_RW;
    }
}
