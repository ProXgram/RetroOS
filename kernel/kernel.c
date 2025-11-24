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

    // 2. Initialize Interrupts & Timer
    timer_init();
    keyboard_init();
    
    size_t memory_bytes = memtest_detect_upper_limit();
    system_set_total_memory((uint32_t)(memory_bytes / 1024));

    // 3. Initialize Scheduler
    scheduler_init();

    background_render();
    timer_set_callback(background_animate);
    
    fs_init();
}

// Wrapper to launch GUI demo as a task
void gui_task_entry(void) {
    // Disable background animation on the main thread so GUI can own screen
    timer_set_callback(NULL);
    gui_demo_run();
    // When GUI exits, it returns here. We spin or kill task.
    while(1) __asm__ volatile("hlt");
}

void kmain(const struct BootInfo* boot_info) {
    boot_sequence(boot_info);

    // Demonstrate Multitasking:
    // Spawn the GUI Demo in a separate thread.
    // The main thread will continue to run the Shell.
    // Note: Both fighting for screen is chaotic, but proves scheduler works.
    // For a cleaner demo, we usually suspend shell, but here we run side-by-side.
    
    kprintf("Spawning GUI Task...\n");
    spawn_task(gui_task_entry);

    // Ring 3 Test Task (Infinite Loop)
    // spawn_user_task(some_user_function); 

    shell_run();
}
