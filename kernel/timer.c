#include "timer.h"
#include "io.h"
#include "syslog.h"
#include "interrupts.h"

/* 1.193182 MHz */
#define PIT_FREQUENCY 1193180

static volatile uint64_t g_ticks = 0;
static int g_freq_hz = 100;

void timer_phase(int hz) {
    if (hz == 0) hz = 100;
    g_freq_hz = hz;

    int divisor = PIT_FREQUENCY / hz;

    outb(0x43, 0x36);             /* Command byte: Channel 0, Access lo/hi, Square Wave */
    outb(0x40, (uint8_t)(divisor & 0xFF));      /* Low byte */
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); /* High byte */
}

void timer_handler(void) {
    g_ticks++;
}

void timer_wait(int ticks) {
    uint64_t end = g_ticks + ticks;
    while (g_ticks < end) {
        __asm__ volatile("hlt");
    }
}

uint64_t timer_get_ticks(void) {
    return g_ticks;
}

uint64_t timer_get_uptime(void) {
    return g_ticks / g_freq_hz;
}

void timer_init(void) {
    /* Initialize to 100Hz (10ms per tick) */
    timer_phase(100);
    interrupts_enable_irq(0); /* Unmask IRQ 0 */
    syslog_write("PIT: System timer initialized at 100Hz");
}
