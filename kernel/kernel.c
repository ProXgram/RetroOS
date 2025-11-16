#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "fs.h"
#include "shell.h"
#include "system.h"
#include "syslog.h"
#include "terminal.h"

static void boot_sequence(const struct BootInfo* boot_info) {
    syslog_init();
    syslog_write("Boot: entered kernel");

    system_cache_boot_info(boot_info);
    const struct BootInfo* cached = system_boot_info();

    terminal_initialize(cached->width, cached->height);
    syslog_write("Boot: terminal initialized");

    background_render();
    fs_init();
    syslog_write("Boot: virtual filesystem mounted");
}

void kmain(const struct BootInfo* boot_info) {
    boot_sequence(boot_info);

    syslog_write("Shell: starting interactive console");
    shell_run();
}
