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
    syslog_write("Boot: entered kernel");

    // Initialize System Hardware Wrappers
    system_cache_boot_info(boot_info);
    const struct BootInfo* cached = system_boot_info();

    // Init graphics/terminal subsystems
    terminal_initialize(cached->width, cached->height);
    syslog_write("Boot: terminal initialized");

    // Initialize core drivers EARLY
    timer_init();
    keyboard_init();
    
    // Detect available memory and update system profile
    size_t memory_bytes = memtest_detect_upper_limit();
    system_set_total_memory((uint32_t)(memory_bytes / 1024));
    syslog_write("Boot: memory detection complete");

    // Render the desktop/background UI for the shell
    background_render();
    
    // Initialize Filesystem
    fs_init();
    syslog_write("Boot: virtual filesystem mounted");
}

void kmain(const struct BootInfo* boot_info) {
    syslog_write("Trace: entered kmain");
    
    boot_sequence(boot_info);

    syslog_write("Shell: starting interactive console");
    shell_run();
}
