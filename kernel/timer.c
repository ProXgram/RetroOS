#include "timer.h"
#include "io.h"
#include "syslog.h"
#include "interrupts.h"
#include "scheduler.h"
#include <stddef.h> 

#define PIT_FREQUENCY 1193180

static volatile uint64_t g_ticks = 0;
static int g_freq_hz = 100;
static timer_callback_t g_callback = NULL;

void timer_phase(int hz) {
    if (hz == 0) hz = 100;
    g_freq_hz = hz;
    int divisor = PIT_FREQUENCY / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_set_callback(timer_callback_t callback) {
    g_callback = callback;
}

void timer_handler(void) {
    g_ticks++;
    
    if (g_callback != NULL && (g_ticks % 4 == 0)) {
        g_callback();
    }

    if (g_ticks % 2 == 0) {
        schedule();
    }
}

void timer_wait(int ticks) {
    uint64_t end = g_ticks + ticks;
    while (g_ticks < end) {
        // FIX: Use 'pause' instead of 'hlt'. 
        // 'hlt' is privileged and will crash User Mode programs (Ring 3).
        __asm__ volatile("pause");
    }
}

uint64_t timer_get_ticks(void) { return g_ticks; }
uint64_t timer_get_uptime(void) { return g_ticks / g_freq_hz; }

void timer_init(void) {
    timer_phase(100);
    g_callback = NULL;
    interrupts_enable_irq(0);
    syslog_write("PIT: System timer initialized");
}
