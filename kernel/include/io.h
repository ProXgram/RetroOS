#ifndef IO_H
#define IO_H

#include <stdint.h>

/* 
 * Optimization: inline assembly avoids function call overhead (call/ret/stack)
 * which is critical for interrupt handlers.
 */

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* IO_H */
