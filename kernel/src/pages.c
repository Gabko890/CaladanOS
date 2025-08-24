#include <stdint.h>
#include <stddef.h>

#define PTE_PRESENT 0x1ULL
#define PTE_RW      0x2ULL
#define PTE_PS      0x80ULL

/* Use different physical addresses to avoid overwriting active page tables */
#define PML4_PHYS       0x10000ULL    /* 64KB */
#define PDPT_LO_PHYS    0x11000ULL    /* 68KB */
#define PD_LO_PHYS      0x12000ULL    /* 72KB */
#define PDPT_HI_PHYS    0x13000ULL    /* 76KB */
#define PD_HI_PHYS      0x14000ULL    /* 80KB */

/* Match ASM stub kernel virtual address */
#define KERNEL_VA       0xFFFFFFFF80000000ULL
#define KERNEL_PMA      0x00200000ULL

void* memset (void *dest, register int val, register size_t len)
{
    register unsigned char *ptr = (unsigned char*)dest;
    while (len-- > 0)
        *ptr++ = val;
    return dest;
}

void setup_page_tables() {
    uint64_t *pml4     = (uint64_t*)PML4_PHYS;
    uint64_t *pdpt_lo  = (uint64_t*)PDPT_LO_PHYS;
    uint64_t *pd_lo    = (uint64_t*)PD_LO_PHYS;
    uint64_t *pdpt_hi  = (uint64_t*)PDPT_HI_PHYS;
    uint64_t *pd_hi    = (uint64_t*)PD_HI_PHYS;

    /* Clear all page tables */
    memset(pml4, 0, 4096);
    memset(pdpt_lo, 0, 4096);
    memset(pd_lo, 0, 4096);
    memset(pdpt_hi, 0, 4096);
    memset(pd_hi, 0, 4096);

    /* PML4[0] -> PDPT_LO (identity mapping) */
    pml4[0] = PDPT_LO_PHYS | PTE_PRESENT | PTE_RW;

    /* PML4[511] -> PDPT_HI (higher half mapping) */
    pml4[511] = PDPT_HI_PHYS | PTE_PRESENT | PTE_RW;

    /* PDPT_LO[0] -> PD_LO */
    pdpt_lo[0] = PD_LO_PHYS | PTE_PRESENT | PTE_RW;

    /* Identity map first 4MiB via two 2MiB huge pages */
    /* PD_LO[0] = 0MiB (0x000000 - 0x1FFFFF) */
    pd_lo[0] = 0x00000000ULL | PTE_PRESENT | PTE_RW | PTE_PS;
    /* PD_LO[1] = 2MiB (0x200000 - 0x3FFFFF) */
    pd_lo[1] = 0x00200000ULL | PTE_PRESENT | PTE_RW | PTE_PS;

    /* PDPT_HI[510] -> PD_HI (for 0xFFFFFFFF80000000) */
    pdpt_hi[510] = PD_HI_PHYS | PTE_PRESENT | PTE_RW;

    /* Map kernel higher half: 2 x 2MiB pages starting at KERNEL_PMA -> KERNEL_VA */
    /* This maps 0x200000 phys -> 0xFFFFFFFF80000000 virt via 2MiB huge page */
    pd_hi[0] = KERNEL_PMA | PTE_PRESENT | PTE_RW | PTE_PS;
    /* Map second 2MiB page for larger kernels */
    pd_hi[1] = (KERNEL_PMA + 0x200000ULL) | PTE_PRESENT | PTE_RW | PTE_PS;
}
