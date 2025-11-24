#include "mouse.h"
#include "io.h"
#include "interrupts.h"
#include "graphics.h"
#include "syslog.h"

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

static uint8_t g_mouse_cycle = 0;
static int8_t  g_mouse_byte[3];
static int     g_mouse_x = 0;
static int     g_mouse_y = 0;
static bool    g_left_btn = false;
static bool    g_right_btn = false;

static void mouse_wait(bool type) {
    uint32_t timeout = 100000;
    if (!type) { // Wait for write
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 0x02) == 0) return;
        }
    } else { // Wait for read
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 0x01) == 1) return;
        }
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait(false);
    outb(MOUSE_PORT_DATA, write);
}

static uint8_t mouse_read(void) {
    mouse_wait(true);
    return inb(MOUSE_PORT_DATA);
}

void mouse_init(void) {
    uint8_t status;

    // Enable Mouse Port
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0xA8);

    // Enable Interrupts
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0x20);
    mouse_wait(true);
    status = (inb(MOUSE_PORT_DATA) | 2);
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait(false);
    outb(MOUSE_PORT_DATA, status);

    // Use default settings
    mouse_write(0xF6);
    mouse_read(); // Acknowledge

    // Enable Data Reporting
    mouse_write(0xF4);
    mouse_read(); // Acknowledge

    // Center Mouse
    g_mouse_x = graphics_get_width() / 2;
    g_mouse_y = graphics_get_height() / 2;

    // Unmask IRQ 12 (Slave PIC line 4)
    interrupts_enable_irq(12);
    
    syslog_write("Mouse: PS/2 initialized");
}

void mouse_handle_interrupt(void) {
    uint8_t status = inb(MOUSE_PORT_STATUS);
    // Check if the interrupt is actually from the mouse (Auxiliary Device)
    // Bit 5 (0x20) of status register is 1 if data comes from mouse
    if (!(status & 0x20)) {
        // If interrupt fired but status says it's not mouse, it might be stray keyboard data?
        // Just read it to clear it if buffer is full
        if (status & 0x01) inb(MOUSE_PORT_DATA);
        return; 
    }

    uint8_t b = inb(MOUSE_PORT_DATA);

    switch(g_mouse_cycle) {
        case 0:
            // Byte 0: Y overflow, X overflow, Y sign, X sign, 1, Middle, Right, Left
            // Bit 3 must be 1 for a standard packet.
            if ((b & 0x08) == 0x08) { 
                g_mouse_byte[0] = b;
                g_mouse_cycle++;
            }
            // If bit 3 isn't 1, we are out of sync. Remain in cycle 0.
            break;
        case 1:
            g_mouse_byte[1] = b;
            g_mouse_cycle++;
            break;
        case 2:
            g_mouse_byte[2] = b;
            g_mouse_cycle = 0;

            // Packet ready
            // Note: PS/2 values are 9-bit two's complement stored in 8 bits + sign bit in byte 0.
            // However, standard cast usually works for small movements.
            // Let's use the sign bits for robustness.
            
            int dx = (int8_t)g_mouse_byte[1];
            int dy = (int8_t)g_mouse_byte[2];
            
            // Overflow bits
            bool x_ovf = (g_mouse_byte[0] & 0x40) != 0;
            bool y_ovf = (g_mouse_byte[0] & 0x80) != 0;

            if (!x_ovf && !y_ovf) {
                // Standard PS/2 mouse Y is inverted relative to screen (Up is positive in math, but lower pixel Y)
                // Wait, in PS/2:
                // Byte 1: X movement
                // Byte 2: Y movement (Positive = Up)
                // Screen Y increases Down. So we subtract dy.
                
                g_mouse_x += dx;
                g_mouse_y -= dy; 

                // Clamp to screen
                int w = graphics_get_width();
                int h = graphics_get_height();
                if (g_mouse_x < 0) g_mouse_x = 0;
                if (g_mouse_x >= w) g_mouse_x = w - 1;
                if (g_mouse_y < 0) g_mouse_y = 0;
                if (g_mouse_y >= h) g_mouse_y = h - 1;

                g_left_btn = (g_mouse_byte[0] & 0x01) != 0;
                g_right_btn = (g_mouse_byte[0] & 0x02) != 0;
            }
            break;
    }
}

MouseState mouse_get_state(void) {
    MouseState s;
    s.x = g_mouse_x;
    s.y = g_mouse_y;
    s.left_button = g_left_btn;
    s.right_button = g_right_btn;
    return s;
}
