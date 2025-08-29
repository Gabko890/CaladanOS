#ifndef SIMPLE_MALLOC_H
#define SIMPLE_MALLOC_H

#include <cldtypes.h>

void* simple_malloc(size_t size);
void simple_free(void* ptr);
void simple_malloc_init(void);

#endif // SIMPLE_MALLOC_H