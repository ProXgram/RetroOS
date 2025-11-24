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
    // Create a task for the currently running kernel code (Main Thread)
    Task* kmain_task = (Task*)kmalloc(sizeof(Task));
    kmain_task->id = g_next_pid++;
    kmain_task->rsp = 0; // Won't be used until we switch away
    kmain_task->is_user = false;
    kmain_task->kernel_stack_top = 0; 
    kmain_task->next = kmain_task; // Circular list

    g_head = kmain_task;
    g_current_task = kmain_task;
    
    syslog_write("Scheduler: Initialized (Multitasking enabled)");
}

static void task_exit_wrapper(void) {
    kprintf("Task %u exited.\n", (unsigned int)g_current_task->id);
    while (1) __asm__ volatile("hlt");
}

void spawn_task(void (*entry_point)(void)) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    uint8_t* stack = (uint8_t*)kmalloc(STACK_SIZE);
    
    new_task->id = g_next_pid++;
    new_task->is_user = false;
    
    // Set up stack for context switch
    uint64_t* sp = (uint64_t*)(stack + STACK_SIZE);
    
    // 1. Return address for context_switch (RIP)
    *(--sp) = (uint64_t)entry_point;
    
    // 2. Callee saved registers (rbx, rbp, r12-r15)
    *(--sp) = 0; // R15
    *(--sp) = 0; // R14
    *(--sp) = 0; // R13
    *(--sp) = 0; // R12
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX
    
    new_task->rsp = (uint64_t)sp;
    new_task->kernel_stack_top = (uint64_t)(stack + STACK_SIZE);

    // Insert into list
    new_task->next = g_head->next;
    g_head->next = new_task;
}

// Spawns a task that starts in Ring 3
// Requires iretq frame construction on the kernel stack
void spawn_user_task(void (*entry_point)(void)) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    uint8_t* kstack = (uint8_t*)kmalloc(STACK_SIZE);
    uint8_t* ustack = (uint8_t*)kmalloc(STACK_SIZE); // User stack
    
    new_task->id = g_next_pid++;
    new_task->is_user = true;
    new_task->kernel_stack_top = (uint64_t)(kstack + STACK_SIZE);

    uint64_t* sp = (uint64_t*)(kstack + STACK_SIZE);
    
    // --- Interrupt Return Frame (for iretq) ---
    // SS (User Data Selector | RPL 3)
    *(--sp) = 0x18 | 3; 
    // RSP (User Stack Pointer)
    *(--sp) = (uint64_t)(ustack + STACK_SIZE);
    // RFLAGS (Interrupts Enabled)
    *(--sp) = 0x202; 
    // CS (User Code Selector | RPL 3)
    *(--sp) = 0x20 | 3;
    // RIP (Entry Point)
    *(--sp) = (uint64_t)entry_point;
    
    // --- Context Switch Frame (for context_switch) ---
    // This part runs in kernel mode to restore the state before iretq
    
    // Return address for context_switch (must point to an iretq stub)
    extern void _iret_stub(); // Defined in entry.asm
    *(--sp) = (uint64_t)_iret_stub;
    
    // Callee saved registers
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

void schedule(void) {
    if (!g_current_task) return;

    Task* next = (Task*)g_current_task->next;
    if (next == g_current_task) return; // Only one task

    Task* prev = g_current_task;
    g_current_task = next;
    
    // If next task is user, ensure TSS has the correct RSP0
    if (next->kernel_stack_top != 0) {
        gdt_set_kernel_stack(next->kernel_stack_top);
    }

    context_switch(&prev->rsp, next->rsp);
}
