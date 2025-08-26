#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H

#include <cldtypes.h>

#define MEMORY_INFO_MAX 32
#define MEMORY_INFO_SYSTEM_RAM
#define MEMORY_INFO_SYSTEM_FRAMEBUFFER

struct memory_info {
    size_t count;
    u64 regions_start[MEMORY_INFO_MAX];
    u64 regions_end[MEMORY_INFO_MAX];
    u8 flags[MEMORY_INFO_MAX];
};

void get_memory_info(
    struct mb2_memory_map*,
    struct mb2_modules_list*,
    void*
);

#endif //  MEMORY_INFO_H
