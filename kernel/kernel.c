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

static void boot_sequence(const struct BootInfo* boot_info) {
    system_cache_boot_info(boot_info);
    const struct BootInfo* cached = system_boot_info();

    terminal_initialize(cached->width, cached->height);
    timer_init();
    keyboard_init();
    
    size_t memory_bytes = memtest_detect_upper_limit();
    system_set_total_memory((uint32_t)(memory_bytes / 1024));

    background_render();
    
    // --- ENABLE AUTOMATIC ANIMATION ---
    // This hooks the background animation to the timer interrupt (IRQ0)
    timer_set_callback(background_animate);
    
    fs_init();
}

void kmain(const struct BootInfo* boot_info) {
    boot_sequence(boot_info);
    shell_run();
}
