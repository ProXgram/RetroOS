#include "mouse.h"
#include "io.h"
#include "interrupts.h"
#include "graphics.h"
#include "syslog.h"

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

// Default sensitivity
static int g_mouse_sensitivity = 1;

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

void mouse_set_sensitivity(int sense) {
    if (sense < 1) sense = 1;
    if (sense > 10) sense = 10;
    g_mouse_sensitivity = sense;
}

int mouse_get_sensitivity(void) {
    return g_mouse_sensitivity;
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
    if (!(status & 0x20)) {
        if (status & 0x01) inb(MOUSE_PORT_DATA);
        return; 
    }

    uint8_t b = inb(MOUSE_PORT_DATA);

    switch(g_mouse_cycle) {
        case 0:
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

            // Apply variable sensitivity
            int dx = (int8_t)g_mouse_byte[1] * g_mouse_sensitivity;
            int dy = (int8_t)g_mouse_byte[2] * g_mouse_sensitivity;
            
            bool x_ovf = (g_mouse_byte[0] & 0x40) != 0;
            bool y_ovf = (g_mouse_byte[0] & 0x80) != 0;

            // Skip update on overflow to prevent erratic jumps
            if (!x_ovf && !y_ovf) {
                g_mouse_x += dx;
                g_mouse_y -= dy; 

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

// test
