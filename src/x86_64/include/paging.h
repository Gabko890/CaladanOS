#ifndef PAGING_H
#define PAGING_H

#define PAGE_PRESENT   0x1
#define PAGE_WRITABLE  0x2
#define PAGE_HUGE      (1ULL << 7)

void map_page(void* virt, void* phys, uint64_t flags);
void unmap_page(void* virt);

#endif
