#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "fs.h"
#include "memtest.h"
#include "shell.h"
#include "system.h"
#include "syslog.h"
#include "terminal.h"
#include "keyboard.h"
#include "mouse.h" // Added include for mouse
#include "timer.h" 
#include "banner.h"
#include "heap.h"
#include "scheduler.h"
#include "gui_demo.h"
#include "kstdio.h"

// Defined in linker script
extern uint8_t __kernel_end[];

static void boot_sequence(const struct BootInfo* boot_info) {
    system_cache_boot_info(boot_info);
    const struct BootInfo* cached = system_boot_info();

    terminal_initialize(cached->width, cached->height);
    
    // 1. Initialize Heap (16MB starting at 8MB mark)
    heap_init((void*)0x800000, 16 * 1024 * 1024);

    // 2. Initialize Interrupts, Timer & Input
    timer_init();
    keyboard_init();
    mouse_init(); // Initialize Mouse Driver
    
    size_t memory_bytes = memtest_detect_upper_limit();
    system_set_total_memory((uint32_t)(memory_bytes / 1024));

    // 3. Initialize Scheduler
    scheduler_init();

    background_render();
    timer_set_callback(background_animate);
    
    fs_init();
}

void kmain(const struct BootInfo* boot_info) {
    boot_sequence(boot_info);

    // NOTE: We do NOT spawn the GUI task automatically anymore.
    // This prevents the GUI from stealing keyboard input from the shell.
    // The user can type 'gui' in the shell to launch the desktop.

    shell_run();
}
