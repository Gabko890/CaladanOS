#ifndef RAM_H
#define RAM_H

#include <multiboot/multiboot2.h>
#include <stdint.h>

uint64_t init_ram_manager(struct mb2_modules_list, struct mb2_memory_map);

#endif