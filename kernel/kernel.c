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

void kmain(const struct BootInfo* boot_info) {
    (void)boot_info;

    terminal_initialize();
    shell_run();
}
