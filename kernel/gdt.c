#include "gdt.h"

#include <stdint.h>
#include <stddef.h>
#include "syslog.h"

struct gdt_entry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

struct gdt_layout {
    struct gdt_entry64 null;
    struct gdt_entry64 code;
    struct gdt_entry64 data;
    struct tss_descriptor tss;
} __attribute__((packed));

_Static_assert(offsetof(struct gdt_layout, null) == 0x00, "Null selector must be at 0x00");
_Static_assert(offsetof(struct gdt_layout, code) == 0x08, "Code selector must be 0x08");
_Static_assert(offsetof(struct gdt_layout, data) == 0x10, "Data selector must be 0x10");
_Static_assert(offsetof(struct gdt_layout, tss) == 0x18, "TSS selector must be 0x18");

enum {
    KERNEL_STACK_SIZE = 8192,
    DOUBLE_FAULT_STACK_SIZE = 4096,
};

uint8_t g_kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
uint8_t* const g_kernel_stack_top = g_kernel_stack + KERNEL_STACK_SIZE;
static uint8_t g_double_fault_stack[DOUBLE_FAULT_STACK_SIZE] __attribute__((aligned(16)));
static struct tss g_tss __attribute__((aligned(16))) = {
    .rsp = {[0] = (uint64_t)(g_kernel_stack + sizeof(g_kernel_stack))},
    .ist = {[0] = (uint64_t)(g_double_fault_stack + sizeof(g_double_fault_stack))},
    .io_map_base = sizeof(struct tss),
};
static struct gdt_layout g_gdt __attribute__((aligned(16)));

static void syslog_write_hex(const char* label, uint64_t value) {
    char buffer[96];
    size_t index = 0;
    while (label[index] != '\0' && index < sizeof(buffer) - 1) {
        buffer[index] = label[index];
        index++;
    }
    if (index < sizeof(buffer) - 1) {
        buffer[index++] = '0';
    }
    if (index < sizeof(buffer) - 1) {
        buffer[index++] = 'x';
    }
    for (int shift = 60; shift >= 0 && index < sizeof(buffer) - 1; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xF);
        buffer[index++] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + (nibble - 10)));
    }
    buffer[index] = '\0';
    syslog_write(buffer);
}

static void halt_on_invalid(const char* message) {
    syslog_write(message);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void gdt_set_entry(struct gdt_entry64* entry, uint32_t base, uint32_t limit, uint8_t access,
                          uint8_t flags) {
    entry->limit_low = (uint16_t)(limit & 0xFFFF);
    entry->base_low = (uint16_t)(base & 0xFFFF);
    entry->base_mid = (uint8_t)((base >> 16) & 0xFF);
    entry->access = access;
    entry->granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    entry->base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss_entry(struct tss_descriptor* entry, uint64_t base, uint32_t limit) {
    entry->limit_low = (uint16_t)(limit & 0xFFFF);
    entry->base_low = (uint16_t)(base & 0xFFFF);
    entry->base_mid = (uint8_t)((base >> 16) & 0xFF);
    entry->access = 0x89; // Available 64-bit TSS
    entry->granularity = (uint8_t)((limit >> 16) & 0x0F);
    entry->base_high = (uint8_t)((base >> 24) & 0xFF);
    entry->base_upper = (uint32_t)(base >> 32);
    entry->reserved = 0;
}

static void gdt_load_descriptor(const struct gdt_descriptor* descriptor) {
    const uint16_t code_selector = 0x08;
    const uint16_t data_selector = 0x10;

    __asm__ volatile(
        "lgdt (%0)\n"
        "mov %1, %%ds\n"
        "mov %1, %%es\n"
        "mov %1, %%ss\n"
        "mov %1, %%fs\n"
        "mov %1, %%gs\n"
        "pushq %2\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : "r"(descriptor), "r"((uint64_t)data_selector), "r"((uint64_t)code_selector)
        : "rax", "memory");
}

static void tss_load(uint16_t selector) {
    __asm__ volatile("ltr %0" : : "r"(selector));
}

void gdt_init(void) {
    syslog_write("Trace: entering gdt_init");
    g_gdt = (struct gdt_layout){0};
    g_tss = (struct tss){0};

    g_tss.io_map_base = (uint16_t)sizeof(g_tss);
    g_tss.rsp[0] = (uint64_t)(g_kernel_stack + sizeof(g_kernel_stack));
    g_tss.ist[0] = (uint64_t)(g_double_fault_stack + sizeof(g_double_fault_stack));

    // Long mode largely ignores limits and attributes; only the code descriptor's
    // L bit matters. Use a flat 64-bit model with zeroed limits and no granularity
    // to avoid any lingering limit checks contributing to a #GP at early boot.
    gdt_set_entry(&g_gdt.null, 0, 0, 0, 0);
    gdt_set_entry(&g_gdt.code, 0, 0, 0x9A, 0x20); // 64-bit code (L)
    gdt_set_entry(&g_gdt.data, 0, 0, 0x92, 0x00); // 64-bit data
    gdt_set_tss_entry(&g_gdt.tss, (uint64_t)&g_tss, (uint32_t)sizeof(g_tss) - 1);

    const struct gdt_descriptor descriptor = {
        .limit = (uint16_t)(sizeof(g_gdt) - 1),
        .base = (uint64_t)&g_gdt,
    };

    syslog_write("Validating GDT selectors against bootloader (0x08/0x10/0x18)");
    syslog_write_hex("GDT base: ", descriptor.base);
    syslog_write_hex("GDT limit: ", descriptor.limit);
    syslog_write_hex("Code descriptor access: ", g_gdt.code.access);
    syslog_write_hex("Data descriptor access: ", g_gdt.data.access);
    syslog_write_hex("TSS descriptor access: ", g_gdt.tss.access);
    syslog_write_hex("TSS descriptor gran: ", g_gdt.tss.granularity);
    syslog_write_hex("TSS.IST[0]: ", g_tss.ist[0]);

    if ((g_gdt.code.access != 0x9A) || (g_gdt.data.access != 0x92)) {
        halt_on_invalid("Invalid GDT access bytes; halting.");
    }

    if ((g_gdt.tss.access & 0x80) == 0) {
        halt_on_invalid("TSS descriptor not marked present; halting.");
    }

    if (g_tss.ist[0] == 0) {
        halt_on_invalid("TSS IST[0] is empty; halting.");
    }

    gdt_load_descriptor(&descriptor);
    tss_load(0x18);
    syslog_write("Trace: gdt_init complete");
}
