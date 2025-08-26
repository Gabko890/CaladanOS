#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H

#include <cldtypes.h>

#define MEMORY_INFO_MAX 16
#define MEMORY_INFO_SYSTEM_RAM 0x01
#define MEMORY_INFO_SYSTEM_FRAMEBUFFER 0x02

struct memory_info {
    size_t count;
    struct memory_region regions[MEMORY_INFO_MAX];
} ;//__attribute__((aligned(16)));//__attribute__((packed));

void get_memory_info(
    struct mb2_memory_map*,
    struct mb2_modules_list*,
    struct memory_info*
);

#endif //  MEMORY_INFO_H
