#include "paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syslog.h"
#include "system.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_RW      (1ull << 1)
#define PAGE_PS      (1ull << 7) // 2MB Page size in PD

#define PAGE_SIZE       0x1000ull
#define HUGE_PAGE_SIZE  0x200000ull // 2MB

extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static uint64_t g_pml4[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_pdpt[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_pd[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_kernel_pt[512] __attribute__((aligned(PAGE_SIZE)));

// Extra PD to map the Framebuffer using 2MB pages
static uint64_t g_framebuffer_pd[512] __attribute__((aligned(PAGE_SIZE)));

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void initialize_identity_map(const struct BootInfo* boot_info) {
    // Map PML4 -> PDPT
    g_pml4[0] = (uint64_t)g_pdpt | (PAGE_PRESENT | PAGE_RW);
    
    // Map PDPT[0] -> PD (Covers 0 - 1GB)
    g_pdpt[0] = (uint64_t)g_pd | (PAGE_PRESENT | PAGE_RW);

    // Fill PD (512 entries * 2MB = 1GB) for Kernel Identity
    g_pd[0] = (uint64_t)g_kernel_pt | (PAGE_PRESENT | PAGE_RW);
    for (size_t i = 1; i < 512; i++) {
        uint64_t base = (uint64_t)i * HUGE_PAGE_SIZE;
        g_pd[i] = base | PAGE_PRESENT | PAGE_RW | PAGE_PS;
    }

    // Fine-grained 4KB pages for the first 2MB (Kernel code/data)
    for (size_t i = 0; i < 512; i++) {
        uint64_t base = (uint64_t)i * PAGE_SIZE;
        g_kernel_pt[i] = base | PAGE_PRESENT | PAGE_RW;
    }

    // --- MAP FRAMEBUFFER (Safe 2MB Pages) ---
    if (boot_info && boot_info->framebuffer != 0) {
        uint64_t fb = boot_info->framebuffer;
        
        // Calculate indices
        size_t pdpt_idx = (fb >> 30) & 0x1FF; // Which 1GB chunk
        size_t pd_idx   = (fb >> 21) & 0x1FF; // Which 2MB chunk
        
        // We only support mapping if it falls into a new 1GB chunk (e.g. > 1GB)
        // or reusing the existing one if we had dynamic logic. 
        // For now, we assume FB is high (e.g. 0xFD000000).
        if (pdpt_idx > 0) {
            // Link PDPT to our Framebuffer PD
            g_pdpt[pdpt_idx] = (uint64_t)g_framebuffer_pd | PAGE_PRESENT | PAGE_RW;

            // Map ~16MB of framebuffer memory (enough for 1024x768x32 which is 3MB)
            // We map 8 entries of 2MB each
            for (size_t i = 0; i < 8; i++) {
                if (pd_idx + i < 512) {
                    uint64_t page_phys = (align_down(fb, HUGE_PAGE_SIZE)) + (i * HUGE_PAGE_SIZE);
                    g_framebuffer_pd[pd_idx + i] = page_phys | PAGE_PRESENT | PAGE_RW | PAGE_PS;
                }
            }
            syslog_write("Paging: Mapped framebuffer using 2MB pages");
        }
    }
}

static void set_range_writable(uint64_t start, uint64_t end, bool writable) {
    if (end <= start) return;

    uint64_t page_start = align_down(start, PAGE_SIZE);
    uint64_t page_end = align_up(end, PAGE_SIZE);

    if (page_start >= HUGE_PAGE_SIZE) return;
    if (page_end > HUGE_PAGE_SIZE) page_end = HUGE_PAGE_SIZE;

    for (uint64_t address = page_start; address < page_end; address += PAGE_SIZE) {
        size_t index = (size_t)(address / PAGE_SIZE);
        uint64_t entry = g_kernel_pt[index];
        if (writable) entry |= PAGE_RW;
        else entry &= ~PAGE_RW;
        g_kernel_pt[index] = entry;
    }
}

static void load_new_tables(void) {
    uint64_t pml4_phys = (uint64_t)g_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

static void enable_write_protect(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ull << 16);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_init(const struct BootInfo* boot_info) {
    syslog_write("Trace: entering paging_init");
    
    initialize_identity_map(boot_info);

    set_range_writable((uint64_t)__text_start, (uint64_t)__text_end, false);
    set_range_writable((uint64_t)__rodata_start, (uint64_t)__rodata_end, false);
    set_range_writable((uint64_t)__data_start, (uint64_t)__bss_end, true);

    load_new_tables();
    enable_write_protect();
    syslog_write("Trace: paging_init complete");
}
