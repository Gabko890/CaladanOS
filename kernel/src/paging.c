#include <stdint.h>
#include <stddef.h>

#define PTE_PRESENT 0x1
#define PTE_RW      0x2
#define PTE_PS      0x80

#define PML4_PHYS 0x1000
#define PDPT_PHYS 0x2000
#define PD_PHYS   0x3000
#define PT_PHYS   0x4000

#define KERNEL_VA 0xFFFF800000000000ULL


void* memset (void *dest, register int val, register size_t len)
{
  register unsigned char *ptr = (unsigned char*)dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

void setup_page_tables() {
    uint64_t *pml4 = (uint64_t*)PML4_PHYS;
    uint64_t *pdpt = (uint64_t*)PDPT_PHYS;
    uint64_t *pd   = (uint64_t*)PD_PHYS;
    uint64_t *pt   = (uint64_t*)PT_PHYS;

    memset(pml4,0,4096);
    memset(pdpt,0,4096);
    memset(pd,0,4096);
    memset(pt,0,4096);

    /********** 1. Identity map first 2 MiB (PD entry, PS=1) **********/
    pd[0] = 0x00000000 | PTE_PRESENT | PTE_RW | PTE_PS;
    pdpt[0] = PD_PHYS | PTE_PRESENT | PTE_RW;
    pml4[0] = PDPT_PHYS | PTE_PRESENT | PTE_RW;

    /********** 2. Higher-half kernel mapping to same physical memory **********/
    /* Compute indices for KERNEL_VA */
    int pml4_index = (KERNEL_VA >> 39) & 0x1FF;
    int pdpt_index = (KERNEL_VA >> 30) & 0x1FF;
    int pd_index   = (KERNEL_VA >> 21) & 0x1FF;
    int pt_index   = (KERNEL_VA >> 12) & 0x1FF;

    /* Link tables for higher-half mapping */
    pml4[pml4_index] = PDPT_PHYS | PTE_PRESENT | PTE_RW;  // reuse same PDPT
    pdpt[pdpt_index] = PD_PHYS | PTE_PRESENT | PTE_RW;    // reuse same PD
    pd[pd_index]     = PT_PHYS | PTE_PRESENT | PTE_RW;    // new PT

    /* Map 4 KiB pages at KERNEL_VA -> phys 0x0 – 0x200000 */
    for(int i=0; i<512; i++) {
        pt[i] = (i*0x1000) | PTE_PRESENT | PTE_RW;
    }
}

