#ifndef PIT_H
#define PIT_H

#include <cldtypes.h>

void pit_init(u32 hz);
u64 pit_ticks(void);
void sleep_ms(u64 ms);

// IRQ0 handler trampoline (called from ASM stub)
void handle_pit(void);

#endif // PIT_H

