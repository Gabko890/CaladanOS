#ifndef PIT_H
#define PIT_H

#include <cldtypes.h>

void pit_init(u32 hz);
u64 pit_ticks(void);
void sleep_ms(u64 ms);

// Query configured PIT frequency (Hz)
u32 pit_get_hz(void);

// Optional per-tick callback (called from IRQ0 handler). Only one supported.
typedef void (*pit_tick_cb_t)(void);
void pit_set_callback(pit_tick_cb_t cb);

// IRQ0 handler trampoline (called from ASM stub)
void handle_pit(void);

#endif // PIT_H
