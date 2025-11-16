#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

struct BootInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t framebuffer;
};

struct system_profile {
    const char* architecture;
    uint32_t memory_total_kb;
    uint32_t memory_used_kb;
};

void system_cache_boot_info(const struct BootInfo* boot_info);
const struct BootInfo* system_boot_info(void);
const struct system_profile* system_profile_info(void);

#endif /* SYSTEM_H */
