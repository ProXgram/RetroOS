#include "mouse.h"
#include "io.h"
#include "interrupts.h"
#include "graphics.h"
#include "syslog.h"

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

// Sensitivity: 1.0 provides the most accurate 1:1 tracking for PS/2
// Higher values cause the guest cursor to move faster than the host cursor,
// leading to immediate desynchronization.
#define MOUSE_SENSITIVITY  1

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
    // We start in the center because QEMU usually centers the cursor when capturing.
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
        if (status & 0x01) inb(MOUSE_PORT_DATA); // Flush garbage
        return; 
    }

    uint8_t b = inb(MOUSE_PORT_DATA);

    switch(g_mouse_cycle) {
        case 0:
            // Byte 0: Y overflow, X overflow, Y sign, X sign, 1, Middle, Right, Left
            // Bit 3 must be 1.
            if ((b & 0x08) == 0x08) { 
                g_mouse_byte[0] = b;
                g_mouse_cycle++;
            }
            break;
        case 1:
            g_mouse_byte[1] = b;
            g_mouse_cycle++;
            break;
        case 2:
            g_mouse_byte[2] = b;
            g_mouse_cycle = 0;

            // Packet ready
            // Use standard 1:1 sensitivity to minimize drift against host cursor
            int dx = (int8_t)g_mouse_byte[1] * MOUSE_SENSITIVITY;
            int dy = (int8_t)g_mouse_byte[2] * MOUSE_SENSITIVITY;
            
            // We ignore overflow bits. Discarding packets on overflow causes
            // the mouse to 'stuck' during fast movement, which feels like lag/desync.
            // It's better to process the clipped movement than no movement.

            // Standard PS/2 mouse Y is inverted relative to screen
            g_mouse_x += dx;
            g_mouse_y -= dy; 

            // Clamp to screen dimensions
            int w = graphics_get_width();
            int h = graphics_get_height();
            
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_x >= w) g_mouse_x = w - 1;
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_y >= h) g_mouse_y = h - 1;

            g_left_btn = (g_mouse_byte[0] & 0x01) != 0;
            g_right_btn = (g_mouse_byte[0] & 0x02) != 0;
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
