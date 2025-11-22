#include "system.h"

#include <stddef.h>

#include "syslog.h"

static struct BootInfo g_boot_info = {
    .width = 80,
    .height = 25,
    .pitch = 80 * 2,
    .bpp = 16,
    .framebuffer = 0xB8000,
};

static struct system_profile g_profile = {
    .architecture = "x86_64",
    .memory_total_kb = 64 * 1024,
    .memory_used_kb = 512,
};

static uint32_t bytes_to_kib(uint64_t bytes) {
    if (bytes == 0) {
        return 0;
    }
    return (uint32_t)((bytes + 1023) / 1024);
}

static void refresh_memory_usage(void) {
    uint64_t estimated_bytes = (uint64_t)g_boot_info.width * (uint64_t)g_boot_info.height * 2u;
    if (g_boot_info.pitch != 0 && g_boot_info.height != 0) {
        estimated_bytes = (uint64_t)g_boot_info.pitch * (uint64_t)g_boot_info.height;
    }
    uint32_t kib = bytes_to_kib(estimated_bytes);
    if (kib < 64) {
        kib = 64;
    }
    g_profile.memory_used_kb = kib;
}

void system_cache_boot_info(const struct BootInfo* boot_info) {
    if (boot_info != NULL) {
        g_boot_info = *boot_info;
    }

    if (g_boot_info.width == 0) {
        g_boot_info.width = 80;
    }
    if (g_boot_info.height == 0) {
        g_boot_info.height = 25;
    }
    if (g_boot_info.pitch == 0) {
        g_boot_info.pitch = g_boot_info.width * 2;
    }
    if (g_boot_info.bpp == 0) {
        g_boot_info.bpp = 16;
    }

    refresh_memory_usage();
    syslog_write("System: hardware descriptors cached");
}

void system_set_total_memory(uint32_t total_kb) {
    g_profile.memory_total_kb = total_kb;
}

const struct BootInfo* system_boot_info(void) {
    return &g_boot_info;
}

const struct system_profile* system_profile_info(void) {
    return &g_profile;
}
