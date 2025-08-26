#ifndef MEMORY_MAPPER_H
#define MEMORY_MAPPER_H

#include "memory_info.h"
#include "cldtypes.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_4K   0x1000ULL
#define PAGE_2M   0x200000ULL

/* x86_64 PTE flags */
#define PTE_PRESENT      (1ULL << 0)
#define PTE_RW           (1ULL << 1)
#define PTE_USER         (1ULL << 2)
#define PTE_PWT          (1ULL << 3)
#define PTE_PCD          (1ULL << 4)
#define PTE_ACCESSED     (1ULL << 5)
#define PTE_DIRTY        (1ULL << 6)
#define PTE_HUGE         (1ULL << 7)
#define PTE_GLOBAL       (1ULL << 8)
#define PTE_NX           (1ULL << 63)

/* Initialize mapper and reserve physical block for page-tables.
 * Returns physical address of PML4 on success, 0 on failure.
 * On success the selected memory_region in minfo is shrunk so the reserved
 * block is no longer available as system RAM.
 */
u64 mm_init(struct memory_info* minfo);

/* Map/unmap. Return 1 on success, 0 on failure. */
u8 mm_map(u64 virtual_addr, u64 physical_addr, u64 flags, size_t page_size);
u8 mm_unmap(u64 virtual_addr, size_t page_size);

/* Helpers */
static inline u8 is_canonical(u64 addr) {
    u64 mask = 0xFFFFULL << 48;
    return ((addr & mask) == 0 || (addr & mask) == mask) ? 1 : 0;
}
static inline u8 is_aligned(u64 addr, size_t page_size) {
    return ((addr & (page_size - 1)) == 0) ? 1 : 0;
}

#endif /* MEMORY_MAPPER_H */

