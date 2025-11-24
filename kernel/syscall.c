#include "syscall.h"
#include "kstdio.h"
#include "scheduler.h"
#include "syslog.h"
#include "ata.h"

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

// --- Dispatcher ---

void syscall_dispatcher(struct syscall_regs* regs) {
    // RAX holds the syscall number (but it wasn't pushed in the block, it's separate)
    // We need to retrieve RAX. However, interrupts don't save RAX automatically 
    // unless we pushed it. 
    // Let's look at entry.asm: we didn't push RAX.
    // BUT, the C compiler might clobber RAX as a return value register.
    // We need to access the RAX from the previous context.
    
    // Actually, in standard ABI, RAX is the return value. 
    // We should probably modify entry.asm to push RAX or read it directly.
    // For simplicity here, we will assume the user put the syscall number in RDI 
    // (first arg) for this specific demo, or fix entry.asm. 
    
    // Let's assume standard convention: RAX is syscall number.
    // We need to grab it from the frame.
    // Since we didn't push it in `isr_syscall`, it's still in the CPU register 
    // until we clobber it. But we are in a C function now.
    
    // FIX: We will rely on the fact that `isr_syscall` didn't push RAX, 
    // so we can't easily get it unless we modify `isr_syscall`.
    // For this implementation, let's assume syscall number is passed in RDI (Arg 1).
    
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
            // RDI=3, RSI=LBA, RDX=Count, RCX=Buffer
            // Note: We pushed RCX, so it's in regs.
            // However, the mapping of registers depends on the push order.
            // regs->rsi is the 2nd argument from user perspective
            sys_disk_read((uint32_t)regs->rsi, (uint8_t)regs->rdx, (uint8_t*)regs->rcx);
            break;
        default:
            kprintf("Unknown Syscall: %d\n", (int)syscall_num);
            break;
    }
}
