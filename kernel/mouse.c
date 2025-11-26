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
static int     g_sensitivity = 1;

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

    // Disable interrupts strictly during setup
    __asm__ volatile("cli");

    // 1. Enable Mouse Port (Command 0xA8)
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0xA8);

    // 2. Enable Interrupts (Read/Modify/Write Config Byte)
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0x20); // Read Controller Config Byte
    mouse_wait(true);
    status = inb(MOUSE_PORT_DATA);
    
    // Set Bit 1 (Enable Mouse IRQ12)
    // Clear Bit 5 (Disable Mouse Port) -> Must be 0 to enable
    status |= 0x02;
    status &= ~0x20; 
    
    mouse_wait(false);
    outb(MOUSE_PORT_CMD, 0x60); // Write Controller Config Byte
    mouse_wait(false);
    outb(MOUSE_PORT_DATA, status);

    // 3. Set Defaults
    mouse_write(0xF6);
    mouse_read(); // Acknowledge

    // 4. Enable Data Reporting
    mouse_write(0xF4);
    mouse_read(); // Acknowledge

    // Initialize position to center
    int w = graphics_get_width();
    int h = graphics_get_height();
    g_mouse_x = (w > 0) ? w / 2 : 400;
    g_mouse_y = (h > 0) ? h / 2 : 300;
    g_sensitivity = 1;
    g_mouse_cycle = 0;

    // Unmask IRQ 12 (Slave PIC line 4)
    interrupts_enable_irq(12);
    
    __asm__ volatile("sti");

    syslog_write("Mouse: PS/2 initialized");
}

void mouse_handle_interrupt(void) {
    uint8_t status = inb(MOUSE_PORT_STATUS);
    
    // Check if there is data to read
    if (!(status & 0x01)) return;

    // Read the data
    uint8_t b = inb(MOUSE_PORT_DATA);

    // If this byte didn't come from the auxiliary device (mouse), ignore it for mouse logic
    // (though we still consumed it from the port to clear the buffer)
    if (!(status & 0x20)) return;

    switch(g_mouse_cycle) {
        case 0:
            // Packet Byte 1
            // Bit 3 should be 1. If not, we are out of sync.
            // However, some scroll mice use different packets.
            // We try to enforce synchronization.
            if ((b & 0x08) == 0x08) { 
                g_mouse_byte[0] = b;
                g_mouse_cycle++;
            } else {
                // Desync detected, reset cycle
                g_mouse_cycle = 0;
            }
            break;
        case 1:
            // Packet Byte 2: X Movement
            g_mouse_byte[1] = b;
            g_mouse_cycle++;
            break;
        case 2:
            // Packet Byte 3: Y Movement
            g_mouse_byte[2] = b;
            g_mouse_cycle = 0;

            // Process Packet
            int8_t raw_x = (int8_t)g_mouse_byte[1];
            int8_t raw_y = (int8_t)g_mouse_byte[2];
            
            // Handle overflow bits in Byte 0
            // Bit 6: X Overflow, Bit 7: Y Overflow
            // If overflow, we can either ignore or clamp. 
            // Ignoring (setting to 0) causes freezing on fast movement.
            // We'll just use the raw values, which might wrap, but better than freezing.
            
            int dx = (int)raw_x;
            int dy = (int)raw_y;

            dx *= g_sensitivity;
            dy *= g_sensitivity;
            
            // Update Position
            // PS/2 Y is bottom-to-top, screen is top-to-bottom -> subtract dy
            g_mouse_x += dx;
            g_mouse_y -= dy; 

            // Clamp to screen dimensions
            int w = graphics_get_width();
            int h = graphics_get_height();
            
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_x >= w) g_mouse_x = w - 1;
            
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_y >= h) g_mouse_y = h - 1;

            // Buttons
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

void mouse_set_sensitivity(int sense) {
    if (sense < 1) sense = 1;
    g_sensitivity = sense;
}

int mouse_get_sensitivity(void) {
    return g_sensitivity;
}
