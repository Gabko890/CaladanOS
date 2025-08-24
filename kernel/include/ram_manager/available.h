#ifndef AVAILABLE_H
#define AVALIABLE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_ENTRIES 16

struct available_entry {
    uint64_t start_addr;
    uint64_t end_addr;
    size_t size;
};

struct available_map {
    uint8_t count;
    struct available_entry entries[MAX_ENTRIES];
};

#endif // AVAILABLE_H
