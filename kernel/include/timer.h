#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_phase(int hz);
void timer_handler(void);

/* Wait for a specific amount of ticks */
void timer_wait(int ticks);

/* Get total ticks since boot */
uint64_t timer_get_ticks(void);

/* Get seconds since boot */
uint64_t timer_get_uptime(void);

#endif
