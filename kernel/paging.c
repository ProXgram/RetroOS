#include "paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syslog.h"
#include "system.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_RW      (1ull << 1)
#define PAGE_USER    (1ull << 2) // Allow Ring 3
#define PAGE_PS      (1ull << 7) 

#define PAGE_SIZE       0x1000ull
#define HUGE_PAGE_SIZE  0x200000ull

extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __bss_end[];

static uint64_t g_pml4[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_pdpt[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_pd[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_kernel_pt[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_framebuffer_pd[512] __attribute__((aligned(PAGE_SIZE)));

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}
static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void initialize_identity_map(const struct BootInfo* boot_info) {
    // Flag all pages as User accessible for the hybrid demo
    uint64_t flags = PAGE_PRESENT | PAGE_RW | PAGE_USER;

    g_pml4[0] = (uint64_t)g_pdpt | flags;
    g_pdpt[0] = (uint64_t)g_pd | flags;

    // 0-1GB
    g_pd[0] = (uint64_t)g_kernel_pt | flags;
    for (size_t i = 1; i < 512; i++) {
        uint64_t base = (uint64_t)i * HUGE_PAGE_SIZE;
        g_pd[i] = base | flags | PAGE_PS;
    }

    // 0-2MB (4KB pages)
    for (size_t i = 0; i < 512; i++) {
        uint64_t base = (uint64_t)i * PAGE_SIZE;
        g_kernel_pt[i] = base | flags;
    }

    if (boot_info && boot_info->framebuffer != 0) {
        uint64_t fb = boot_info->framebuffer;
        size_t pdpt_idx = (fb >> 30) & 0x1FF;
        size_t pd_idx   = (fb >> 21) & 0x1FF;
        
        if (pdpt_idx > 0) {
            g_pdpt[pdpt_idx] = (uint64_t)g_framebuffer_pd | flags;
            for (size_t i = 0; i < 8; i++) {
                if (pd_idx + i < 512) {
                    uint64_t page_phys = (align_down(fb, HUGE_PAGE_SIZE)) + (i * HUGE_PAGE_SIZE);
                    g_framebuffer_pd[pd_idx + i] = page_phys | flags | PAGE_PS;
                }
            }
        }
    }
}

static void load_new_tables(void) {
    uint64_t pml4_phys = (uint64_t)g_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void paging_init(const struct BootInfo* boot_info) {
    initialize_identity_map(boot_info);
    load_new_tables();
    syslog_write("Paging: Initialized (User Access Enabled)");
}
