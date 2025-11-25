#include "syscall.h"
#include "kstdio.h"
#include "scheduler.h"
#include "syslog.h"
#include "io.h"
#include "mouse.h"

// Definition of the register state pushed by isr_syscall in entry.asm
struct syscall_regs {
    uint64_t rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    // Pushed by CPU on interrupt + error code/rip
    uint64_t rip, cs, rflags, rsp, ss; 
};

// --- Syscall Implementations ---

static void sys_yield(void) {
    schedule();
}

static void sys_exit(void) {
    kprintf("Task exited via syscall.\n");
    // Loop forever (scheduler will switch away)
    while(1) { schedule(); } 
}

static void sys_log(const char* msg) {
    // In a real OS, check pointer validity here!
    syslog_write(msg);
}

static void sys_shutdown(void) {
    syslog_write("Syscall: Shutdown requested");
    outw(0x604, 0x2000); // QEMU
    outw(0xB004, 0x2000); // Bochs
    outw(0x4004, 0x3400); // VirtualBox
}

static void sys_get_mouse(MouseState* user_struct) {
    if (user_struct) {
        MouseState k_state = mouse_get_state();
        *user_struct = k_state;
    }
}

// --- Dispatcher ---

void syscall_dispatcher(struct syscall_regs* regs) {
    // Syscall number passed in RDI (First arg in System V ABI)
    uint64_t syscall_num = regs->rdi; 

    switch (syscall_num) {
        case 0: // Yield
            sys_yield();
            break;
        case 1: // Exit
            sys_exit();
            break;
        case 2: // Log
            // Msg pointer in RSI
            sys_log((const char*)regs->rsi);
            break;
        case 4: // Shutdown
            sys_shutdown();
            break;
        case 5: // Get Mouse
            // Pointer in RSI
            sys_get_mouse((MouseState*)regs->rsi);
            break;
        default:
            // Unknown syscall
            break;
    }
}
