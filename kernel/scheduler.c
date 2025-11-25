#include "scheduler.h"
#include "heap.h"
#include "syslog.h"
#include "gdt.h"
#include "kstdio.h"

static Task* g_current_task = NULL;
static Task* g_head = NULL;
static uint64_t g_next_pid = 1;

#define STACK_SIZE 16384

void scheduler_init(void) {
    Task* kmain_task = (Task*)kmalloc(sizeof(Task));
    kmain_task->id = g_next_pid++;
    kmain_task->rsp = 0; 
    kmain_task->is_user = false;
    kmain_task->state = TASK_READY;
    kmain_task->kernel_stack_top = 0; 
    kmain_task->next = kmain_task; // Circular list

    g_head = kmain_task;
    g_current_task = kmain_task;
    
    syslog_write("Scheduler: Initialized (Multitasking enabled)");
}

void spawn_task(void (*entry_point)(void)) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    uint8_t* stack = (uint8_t*)kmalloc(STACK_SIZE);
    
    new_task->id = g_next_pid++;
    new_task->is_user = false;
    new_task->state = TASK_READY;
    
    uint64_t* sp = (uint64_t*)(stack + STACK_SIZE);
    
    // Return address for context_switch
    *(--sp) = (uint64_t)entry_point;
    
    // Callee saved registers
    *(--sp) = 0; // R15
    *(--sp) = 0; // R14
    *(--sp) = 0; // R13
    *(--sp) = 0; // R12
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX
    
    new_task->rsp = (uint64_t)sp;
    new_task->kernel_stack_top = (uint64_t)(stack + STACK_SIZE);

    new_task->next = g_head->next;
    g_head->next = new_task;
}

void spawn_user_task(void (*entry_point)(void)) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    uint8_t* kstack = (uint8_t*)kmalloc(STACK_SIZE);
    uint8_t* ustack = (uint8_t*)kmalloc(STACK_SIZE);
    
    new_task->id = g_next_pid++;
    new_task->is_user = true;
    new_task->state = TASK_READY;
    new_task->kernel_stack_top = (uint64_t)(kstack + STACK_SIZE);

    uint64_t* sp = (uint64_t*)(kstack + STACK_SIZE);
    
    // IRETQ Frame
    *(--sp) = 0x18 | 3; // SS
    *(--sp) = (uint64_t)(ustack + STACK_SIZE); // RSP
    *(--sp) = 0x202; // RFLAGS
    *(--sp) = 0x20 | 3; // CS
    *(--sp) = (uint64_t)entry_point; // RIP
    
    // Context Switch Frame
    extern void _iret_stub();
    *(--sp) = (uint64_t)_iret_stub;
    
    *(--sp) = 0; // R15
    *(--sp) = 0; // R14
    *(--sp) = 0; // R13
    *(--sp) = 0; // R12
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX

    new_task->rsp = (uint64_t)sp;
    
    new_task->next = g_head->next;
    g_head->next = new_task;
}

void exit_current_task(void) {
    // We cannot free the stack we are currently using.
    // Mark as dead, and the scheduler will simply skip it.
    // In a real OS, a separate "reaper" thread would free these.
    __asm__ volatile("cli");
    if (g_current_task) {
        g_current_task->state = TASK_DEAD;
    }
    __asm__ volatile("sti");
    
    // Yield immediately to switch away
    schedule();
    
    // Should never reach here
    while(1);
}

void schedule(void) {
    if (!g_current_task) return;

    Task* start_task = g_current_task;
    Task* next = (Task*)g_current_task->next;

    // Find next READY task
    while (next != start_task) {
        if (next->state == TASK_READY) break;
        next = (Task*)next->next;
    }

    // If we looped all the way around, check if current is ready
    if (next == start_task && start_task->state != TASK_READY) {
        // All tasks are dead. In a real OS, idle task would run.
        // For now, halt.
        syslog_write("Scheduler: All tasks dead/waiting.");
        while(1) __asm__ volatile("hlt");
    }

    if (next == g_current_task) return; // No switch needed

    Task* prev = g_current_task;
    g_current_task = next;
    
    if (next->kernel_stack_top != 0) {
        gdt_set_kernel_stack(next->kernel_stack_top);
    }

    context_switch(&prev->rsp, next->rsp);
}
