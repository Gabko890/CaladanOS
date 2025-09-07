#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <cldtypes.h>

// Interrupt handler function pointer type
typedef void (*interrupt_handler_t)(void);

// Initialize interrupt system
void interrupts_init(void);

// Register an interrupt handler
void register_interrupt_handler(u8 interrupt_num, interrupt_handler_t handler);

// Enable/disable interrupts globally
void interrupts_enable(void);
void interrupts_disable(void);

// Common interrupt handlers
void default_interrupt_handler(void);

// CPU Exception handlers
void division_error_handler(void);
void debug_exception_handler(void);
void nmi_handler(void);
void breakpoint_handler(void);
void overflow_handler(void);
void bound_range_exceeded_handler(void);
void invalid_opcode_handler(void);
void device_not_available_handler(void);
void double_fault_handler(void);
void invalid_tss_handler(void);
void segment_not_present_handler(void);
void stack_segment_fault_handler(void);
void general_protection_fault_handler(void);
void page_fault_handler(void);
void x87_floating_point_handler(void);
void alignment_check_handler(void);
void machine_check_handler(void);
void simd_floating_point_handler(void);
void virtualization_exception_handler(void);

#endif // INTERRUPTS_H