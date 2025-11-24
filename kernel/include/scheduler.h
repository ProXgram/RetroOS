#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t id;
    uint64_t rsp;
    uint64_t kernel_stack_top; // For Ring 3 -> 0 transitions
    bool is_user;
    void* next;
} Task;

void scheduler_init(void);
void spawn_task(void (*entry_point)(void));
void spawn_user_task(void (*entry_point)(void));
void schedule(void);

// Assembly helper
extern void context_switch(uint64_t* old_sp_ptr, uint64_t new_sp);

#endif
