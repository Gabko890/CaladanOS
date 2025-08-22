#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

// Interrupt handler function pointer type
typedef void (*interrupt_handler_t)(void);

// Initialize interrupt system
void interrupts_init(void);

// Register an interrupt handler
void register_interrupt_handler(uint8_t interrupt_num, interrupt_handler_t handler);

// Enable/disable interrupts globally
void interrupts_enable(void);
void interrupts_disable(void);

// Common interrupt handlers
void default_interrupt_handler(void);

#endif // INTERRUPTS_H