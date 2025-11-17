#include "paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syslog.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_RW      (1ull << 1)
#define PAGE_PS      (1ull << 7)

#define PAGE_SIZE       0x1000ull
#define HUGE_PAGE_SIZE  0x200000ull

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

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void initialize_identity_map(void) {
    g_pml4[0] = (uint64_t)g_pdpt | (PAGE_PRESENT | PAGE_RW);
    g_pdpt[0] = (uint64_t)g_pd | (PAGE_PRESENT | PAGE_RW);

    g_pd[0] = (uint64_t)g_kernel_pt | (PAGE_PRESENT | PAGE_RW);
    for (size_t i = 1; i < 512; i++) {
        uint64_t base = (uint64_t)i * HUGE_PAGE_SIZE;
        g_pd[i] = base | PAGE_PRESENT | PAGE_RW | PAGE_PS;
    }

    for (size_t i = 0; i < 512; i++) {
        uint64_t base = (uint64_t)i * PAGE_SIZE;
        g_kernel_pt[i] = base | PAGE_PRESENT | PAGE_RW;
    }
}

static void set_range_writable(uint64_t start, uint64_t end, bool writable) {
    if (end <= start) {
        return;
    }

    uint64_t page_start = align_down(start, PAGE_SIZE);
    uint64_t page_end = align_up(end, PAGE_SIZE);

    if (page_start >= HUGE_PAGE_SIZE) {
        return;
    }

    if (page_end > HUGE_PAGE_SIZE) {
        page_end = HUGE_PAGE_SIZE;
    }

    for (uint64_t address = page_start; address < page_end; address += PAGE_SIZE) {
        size_t index = (size_t)(address / PAGE_SIZE);
        uint64_t entry = g_kernel_pt[index];
        if (writable) {
            entry |= PAGE_RW;
        } else {
            entry &= ~PAGE_RW;
        }
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

void paging_init(void) {
    syslog_write("Trace: entering paging_init");
    initialize_identity_map();

    set_range_writable((uint64_t)__text_start, (uint64_t)__text_end, false);
    set_range_writable((uint64_t)__rodata_start, (uint64_t)__rodata_end, false);
    set_range_writable((uint64_t)__data_start, (uint64_t)__bss_end, true);

    load_new_tables();
    enable_write_protect();
    syslog_write("Trace: paging_init complete");
}
