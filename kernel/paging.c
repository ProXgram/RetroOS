#include "paging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT (1ull << 0)
#define PAGE_RW      (1ull << 1)
#define PAGE_PS      (1ull << 7)

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ull

#define PAGE_SIZE       0x1000ull
#define HUGE_PAGE_SIZE  0x200000ull

extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static uint64_t g_kernel_pt[512] __attribute__((aligned(PAGE_SIZE)));

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t* current_pml4(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t*)(cr3 & PAGE_ADDR_MASK);
}

static uint64_t* current_pdpt(uint64_t* pml4) {
    return (uint64_t*)((uint64_t)pml4[0] & PAGE_ADDR_MASK);
}

static uint64_t* current_pd(uint64_t* pdpt) {
    return (uint64_t*)((uint64_t)pdpt[0] & PAGE_ADDR_MASK);
}

static void initialize_identity_pt(void) {
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

static void reload_cr3(uint64_t* pml4) {
    uint64_t phys = (uint64_t)pml4 & PAGE_ADDR_MASK;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static void enable_write_protect(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ull << 16);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_init(void) {
    uint64_t* pml4 = current_pml4();
    uint64_t* pdpt = current_pdpt(pml4);
    uint64_t* pd = current_pd(pdpt);

    initialize_identity_pt();
    pd[0] = ((uint64_t)g_kernel_pt & PAGE_ADDR_MASK) | PAGE_PRESENT | PAGE_RW;

    set_range_writable((uint64_t)__text_start, (uint64_t)__text_end, false);
    set_range_writable((uint64_t)__rodata_start, (uint64_t)__rodata_end, false);
    set_range_writable((uint64_t)__data_start, (uint64_t)__bss_end, true);

    reload_cr3(pml4);
    enable_write_protect();
}
