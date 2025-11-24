#include "syscall.h"
#include "kstdio.h"
#include "scheduler.h"
#include "syslog.h"
#include "ata.h"
#include "io.h"
#include "mouse.h"

// Definition of the register state pushed by isr_syscall
struct syscall_regs {
    uint64_t rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    uint64_t rip, cs, rflags, rsp, ss; // Pushed by CPU on interrupt
};

// --- Syscall Implementations ---

static void sys_yield(void) {
    schedule();
}

static void sys_exit(void) {
    kprintf("Process requested exit.\n");
    while(1) { schedule(); } // Simple hang/yield loop for now
}

static void sys_log(const char* msg) {
    // Security TODO: Validate 'msg' pointer is in user range!
    syslog_write(msg);
}

// Example of a driver interaction via syscall
static int sys_disk_read(uint32_t lba, uint8_t count, uint8_t* buffer) {
    // Security TODO: Validate 'buffer' pointer!
    if (ata_read(lba, count, buffer)) {
        return 0; // Success
    }
    return -1; // Error
}

static void sys_shutdown(void) {
    syslog_write("Syscall: Shutdown requested");
    outw(0x604, 0x2000); // QEMU Shutdown
    outw(0xB004, 0x2000); // Bochs Shutdown
    outw(0x4004, 0x3400); // VirtualBox Shutdown
}

static void sys_get_mouse(MouseState* user_struct) {
    // In a real OS, validate pointer first!
    if (user_struct) {
        MouseState k_state = mouse_get_state();
        *user_struct = k_state;
    }
}

// --- Dispatcher ---

void syscall_dispatcher(struct syscall_regs* regs) {
    // Syscall number passed in RDI (Argument 1)
    uint64_t syscall_num = regs->rdi; 

    switch (syscall_num) {
        case 0: // Yield
            sys_yield();
            break;
        case 1: // Exit
            sys_exit();
            break;
        case 2: // Log
            sys_log((const char*)regs->rsi);
            break;
        case 3: // Disk Read
            sys_disk_read((uint32_t)regs->rsi, (uint8_t)regs->rdx, (uint8_t*)regs->rcx);
            break;
        case 4: // Shutdown
            sys_shutdown();
            break;
        case 5: // Get Mouse State
            // RSI contains the pointer to the User struct
            sys_get_mouse((MouseState*)regs->rsi);
            break;
        default:
            kprintf("Unknown Syscall: %d\n", (int)syscall_num);
            break;
    }
}
