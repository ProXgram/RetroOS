#include <stddef.h>
#include <stdint.h>

#include "shell.h"
#include "terminal.h"

/*
 * Basic boot information provided by the second-stage loader.  The loader
 * passes a pointer to this structure in RDI before jumping into the kernel's
 * entry point.
 */
struct BootInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t framebuffer;
};

static struct BootInfo g_boot_info = {
    .width = 80,
    .height = 25,
    .pitch = 0,
    .bpp = 0,
    .framebuffer = 0,
};

static void cache_boot_info(const struct BootInfo* boot_info) {
    if (boot_info == NULL) {
        return;
    }

    g_boot_info = *boot_info;
}

void kmain(const struct BootInfo* boot_info) {
    cache_boot_info(boot_info);

    terminal_initialize(g_boot_info.width, g_boot_info.height);
    shell_run();
}
