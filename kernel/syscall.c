#include "syscall.h"
#include "kstdio.h"
#include "scheduler.h"
#include "syslog.h"
#include "io.h"
#include "mouse.h"
#include "heap.h"

struct syscall_regs {
    uint64_t rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    uint64_t rip, cs, rflags, rsp, ss; 
};

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, (uint8_t)reg);
    return inb(CMOS_DATA);
}

static void sys_yield(void) { schedule(); }

static void sys_exit(void) {
    syslog_write("Syscall: Task exited");
    exit_current_task();
}

static void sys_log(const char* msg) { syslog_write(msg); }

static void sys_shutdown(void) {
    syslog_write("Syscall: Shutdown");
    outw(0x604, 0x2000); 
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
}

static void sys_get_mouse(MouseState* user_struct) {
    if (user_struct) {
        MouseState k_state = mouse_get_state();
        *user_struct = k_state;
    }
}

static void* sys_malloc(size_t size) { return kmalloc(size); }
static void sys_free(void* ptr) { kfree(ptr); }

static void sys_get_time(char* buffer) {
    while (get_rtc_register(0x0A) & 0x80); 
    uint8_t m = get_rtc_register(0x02);
    uint8_t h = get_rtc_register(0x04);
    m = (m & 0x0F) + ((m / 16) * 10);
    h = ((h & 0x0F) + ((h / 16) * 10));
    buffer[0] = '0' + (h / 10); buffer[1] = '0' + (h % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (m / 10); buffer[4] = '0' + (m % 10);
    buffer[5] = 0;
}

// Returns value to be placed in RAX
uint64_t syscall_dispatcher(struct syscall_regs* regs) {
    uint64_t syscall_num = regs->rdi; 
    uint64_t ret = 0;

    switch (syscall_num) {
        case 0: sys_yield(); break;
        case 1: sys_exit(); break;
        case 2: sys_log((const char*)regs->rsi); break;
        case 4: sys_shutdown(); break;
        case 5: sys_get_mouse((MouseState*)regs->rsi); break;
        case 6: ret = (uint64_t)sys_malloc((size_t)regs->rsi); break;
        case 7: sys_free((void*)regs->rsi); break;
        case 8: sys_get_time((char*)regs->rsi); break;
    }
    return ret;
}
