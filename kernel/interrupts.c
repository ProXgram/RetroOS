#include "interrupts.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "syslog.h"
#include "io.h"

struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];

enum {
    PIC1_COMMAND = 0x20,
    PIC1_DATA = 0x21,
    PIC2_COMMAND = 0xA0,
    PIC2_DATA = 0xA1,
};

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

static void pic_remap_and_mask(void) {
    /* Masked remap to move IRQs off exception vectors. */

    /* Start the initialization sequence (cascade, expect ICW4). */
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    /* Set vector offsets to 0x20-0x2F. */
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    /* Tell the master that a slave is on IRQ2, and the slave its cascade identity. */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* Set 8086/88 (MCS-80/85) mode. */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Leave everything masked until real IRQ handlers exist. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    syslog_write("PIC remapped to 0x20/0x28 and masked");
}

static void pic_send_eoi(uint8_t vector) {
    const uint8_t irq = (uint8_t)(vector - 0x20);

    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

static const char* const EXCEPTION_NAMES[] = {
    "Divide-by-zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack fault",
    "General protection",
    "Page fault",
    "Reserved",
    "x87 floating point",
    "Alignment check",
    "Machine check",
    "SIMD floating point",
    "Virtualization",
    "Control protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
};

#define VGA_COLUMNS 80
#define VGA_ROWS 25
#define PANIC_COLOR 0x4F

static size_t panic_row;

static void panic_clear_screen(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    const uint16_t entry = ((uint16_t)PANIC_COLOR << 8) | ' ';
    for (size_t i = 0; i < VGA_COLUMNS * VGA_ROWS; i++) {
        vga[i] = entry;
    }
    panic_row = 0;
}

static void panic_write_line(const char* text) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    if (panic_row >= VGA_ROWS) {
        panic_row = VGA_ROWS - 1;
    }
    size_t column = 0;
    while (text[column] != '\0' && column < VGA_COLUMNS) {
        vga[panic_row * VGA_COLUMNS + column] = ((uint16_t)PANIC_COLOR << 8) | (uint8_t)text[column];
        column++;
    }
    for (; column < VGA_COLUMNS; column++) {
        vga[panic_row * VGA_COLUMNS + column] = ((uint16_t)PANIC_COLOR << 8) | ' ';
    }
    panic_row++;
}

static void panic_write_hex_line(const char* label, uint64_t value) {
    char buffer[80];
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
    panic_write_line(buffer);
}

static void panic_write_vector_line(uint8_t vector) {
    const char* name = (vector < (sizeof(EXCEPTION_NAMES) / sizeof(EXCEPTION_NAMES[0])))
                           ? EXCEPTION_NAMES[vector]
                           : "Reserved";
    char buffer[80];
    size_t index = 0;
    const char prefix[] = "Vector 0x";
    while (index < sizeof(prefix) - 1 && index < sizeof(buffer) - 1) {
        buffer[index] = prefix[index];
        index++;
    }
    uint8_t high = (uint8_t)((vector >> 4) & 0xF);
    uint8_t low = (uint8_t)(vector & 0xF);
    if (index < sizeof(buffer) - 1) {
        buffer[index++] = (char)(high < 10 ? ('0' + high) : ('A' + (high - 10)));
    }
    if (index < sizeof(buffer) - 1) {
        buffer[index++] = (char)(low < 10 ? ('0' + low) : ('A' + (low - 10)));
    }
    if (index < sizeof(buffer) - 2) {
        buffer[index++] = ' ';
        buffer[index++] = '(';
    }
    size_t name_index = 0;
    while (name[name_index] != '\0' && index < sizeof(buffer) - 1) {
        buffer[index++] = name[name_index++];
    }
    if (index < sizeof(buffer) - 1) {
        buffer[index++] = ')';
    }
    buffer[index] = '\0';
    panic_write_line(buffer);
}

static void exception_panic(uint8_t vector,
                            uint64_t error_code,
                            bool has_error_code,
                            const struct interrupt_frame* frame) {
    panic_clear_screen();
    panic_write_line("!!! CPU EXCEPTION !!!");
    panic_write_vector_line(vector);
    if (has_error_code) {
        panic_write_hex_line("Error code: ", error_code);
    }
    if (frame != NULL) {
        panic_write_hex_line("RIP: ", frame->rip);
        panic_write_hex_line("CS: ", frame->cs);
        panic_write_hex_line("RFLAGS: ", frame->rflags);
        panic_write_hex_line("RSP: ", frame->rsp);
        panic_write_hex_line("SS: ", frame->ss);
    }
    panic_write_line("System halted.");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

#define DECLARE_NOERR_HANDLER(num)                                                               \
    __attribute__((interrupt)) static void handler_##num(struct interrupt_frame* frame) {        \
        exception_panic((uint8_t)(num), 0, false, frame);                                         \
    }

#define DECLARE_ERR_HANDLER(num)                                                                 \
    __attribute__((interrupt)) static void handler_##num(struct interrupt_frame* frame,          \
                                                         uint64_t error_code) {                  \
        exception_panic((uint8_t)(num), error_code, true, frame);                                 \
    }

DECLARE_NOERR_HANDLER(0);
DECLARE_NOERR_HANDLER(1);
__attribute__((interrupt)) static void handler_2(struct interrupt_frame* frame) {
    (void)frame;
}
DECLARE_NOERR_HANDLER(3);
DECLARE_NOERR_HANDLER(4);
DECLARE_NOERR_HANDLER(5);
DECLARE_NOERR_HANDLER(6);
DECLARE_NOERR_HANDLER(7);
DECLARE_ERR_HANDLER(8);
DECLARE_NOERR_HANDLER(9);
DECLARE_ERR_HANDLER(10);
DECLARE_ERR_HANDLER(11);
DECLARE_ERR_HANDLER(12);
DECLARE_ERR_HANDLER(13);
DECLARE_ERR_HANDLER(14);
DECLARE_NOERR_HANDLER(15);
DECLARE_NOERR_HANDLER(16);
DECLARE_ERR_HANDLER(17);
DECLARE_NOERR_HANDLER(18);
DECLARE_NOERR_HANDLER(19);
DECLARE_NOERR_HANDLER(20);
DECLARE_ERR_HANDLER(21);
DECLARE_NOERR_HANDLER(22);
DECLARE_NOERR_HANDLER(23);
DECLARE_NOERR_HANDLER(24);
DECLARE_NOERR_HANDLER(25);
DECLARE_NOERR_HANDLER(26);
DECLARE_NOERR_HANDLER(27);
DECLARE_NOERR_HANDLER(28);
DECLARE_NOERR_HANDLER(29);
DECLARE_NOERR_HANDLER(30);
DECLARE_NOERR_HANDLER(31);

__attribute__((interrupt)) static void handler_irq_master(struct interrupt_frame* frame) {
    (void)frame;
    pic_send_eoi(0x20);
}

__attribute__((interrupt)) static void handler_irq_slave(struct interrupt_frame* frame) {
    (void)frame;
    pic_send_eoi(0x28);
}

static void idt_set_gate(uint8_t vector, void* handler) {
    uint64_t address = (uint64_t)handler;
    g_idt[vector].offset_low = (uint16_t)(address & 0xFFFF);
    g_idt[vector].selector = 0x08;
    g_idt[vector].ist = 0;
    g_idt[vector].type_attr = 0x8E;
    g_idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFF);
    g_idt[vector].zero = 0;
}

static void idt_set_gate_with_ist(uint8_t vector, void* handler, uint8_t ist) {
    uint64_t address = (uint64_t)handler;
    g_idt[vector].offset_low = (uint16_t)(address & 0xFFFF);
    g_idt[vector].selector = 0x08;
    g_idt[vector].ist = ist;
    g_idt[vector].type_attr = 0x8E;
    g_idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFF);
    g_idt[vector].zero = 0;
}

void interrupts_init(void) {
    syslog_write("Trace: entering interrupts_init");

    pic_remap_and_mask();

    idt_set_gate(0, handler_0);
    idt_set_gate(1, handler_1);
    idt_set_gate(2, handler_2);
    idt_set_gate(3, handler_3);
    idt_set_gate(4, handler_4);
    idt_set_gate(5, handler_5);
    idt_set_gate(6, handler_6);
    idt_set_gate(7, handler_7);
    idt_set_gate_with_ist(8, handler_8, 1);
    idt_set_gate(9, handler_9);
    idt_set_gate(10, handler_10);
    idt_set_gate(11, handler_11);
    idt_set_gate(12, handler_12);
    idt_set_gate(13, handler_13);
    idt_set_gate(14, handler_14);
    idt_set_gate(15, handler_15);
    idt_set_gate(16, handler_16);
    idt_set_gate(17, handler_17);
    idt_set_gate(18, handler_18);
    idt_set_gate(19, handler_19);
    idt_set_gate(20, handler_20);
    idt_set_gate(21, handler_21);
    idt_set_gate(22, handler_22);
    idt_set_gate(23, handler_23);
    idt_set_gate(24, handler_24);
    idt_set_gate(25, handler_25);
    idt_set_gate(26, handler_26);
    idt_set_gate(27, handler_27);
    idt_set_gate(28, handler_28);
    idt_set_gate(29, handler_29);
    idt_set_gate(30, handler_30);
    idt_set_gate(31, handler_31);

    for (uint8_t vector = 0x20; vector < 0x28; vector++) {
        idt_set_gate(vector, handler_irq_master);
    }
    for (uint8_t vector = 0x28; vector < 0x30; vector++) {
        idt_set_gate(vector, handler_irq_slave);
    }

    const struct idt_descriptor descriptor = {
        .limit = (uint16_t)(sizeof(g_idt) - 1),
        .base = (uint64_t)g_idt,
    };

    syslog_write_hex("IDT base: ", descriptor.base);
    syslog_write_hex("IDT limit: ", descriptor.limit);
    syslog_write_hex("Vector 8 selector: ", g_idt[8].selector);
    syslog_write_hex("Vector 8 IST: ", g_idt[8].ist);

    if (g_idt[8].selector != 0x08) {
        halt_on_invalid("IDT vector 8 selector does not match kernel code segment; halting.");
    }

    if (g_idt[8].ist != 1) {
        halt_on_invalid("IDT vector 8 IST index is not 1; halting.");
    }

    __asm__ volatile("lidt %0" : : "m"(descriptor));
    syslog_write("Trace: interrupts_init complete");
}
