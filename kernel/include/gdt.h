#ifndef GDT_H
#define GDT_H

#include <stdint.h>

extern uint8_t g_kernel_stack[];
extern uint8_t* const g_kernel_stack_top;

void gdt_init(void);

#endif /* GDT_H */
