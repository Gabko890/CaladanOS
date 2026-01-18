#include <pit/pit.h>
#include <portio.h>
#include <pic.h>
#include <interrupts/interrupts.h>

// PIT ports
#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43

static volatile u64 g_ticks = 0;
static u32 g_hz = 0;

void handle_pit(void) {
    g_ticks++;
    pic_send_eoi(0);
}

void pit_init(u32 hz) {
    if (hz == 0) hz = 1000; // default 1 kHz
    g_hz = hz;
    u32 divisor = 1193182 / hz;
    if (divisor == 0) divisor = 1;
    // Command: channel 0, access lobyte/hibyte, mode 3 (square wave), binary
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFF));

    // Register IRQ0 handler (vector 32)
    extern void irq0_handler(void);
    register_interrupt_handler(32, &irq0_handler);
    // Enable IRQ0 on PIC
    pic_enable_irq(0);
}

u64 pit_ticks(void) {
    return g_ticks;
}

void sleep_ms(u64 ms) {
    if (g_hz == 0) return;
    u64 start = g_ticks;
    u64 target_ticks = (ms * g_hz) / 1000ULL;
    if (target_ticks == 0) target_ticks = 1; // minimum 1 tick
    u64 until = start + target_ticks;
    while (g_ticks < until) {
        __asm__ volatile ("hlt");
    }
}
