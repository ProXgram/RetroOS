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

static void boot_sequence(const struct BootInfo* boot_info) {
    syslog_write("Boot: entered kernel");

    system_cache_boot_info(boot_info);
    const struct BootInfo* cached = system_boot_info();

    terminal_initialize(cached->width, cached->height);
    syslog_write("Boot: terminal initialized");

    // Detect available memory and update system profile
    size_t memory_bytes = memtest_detect_upper_limit();
    system_set_total_memory((uint32_t)(memory_bytes / 1024));
    syslog_write("Boot: memory detection complete");

    background_render();
    fs_init();
    syslog_write("Boot: virtual filesystem mounted");

    timer_init();  
    keyboard_init();
}

void kmain(const struct BootInfo* boot_info) {
    syslog_write("Trace: entered kmain");
    boot_sequence(boot_info);

    syslog_write("Shell: starting interactive console");
    shell_run();
}
