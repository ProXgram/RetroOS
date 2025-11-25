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

    // Center Mouse
    g_mouse_x = graphics_get_width() / 2;
    g_mouse_y = graphics_get_height() / 2;

    // Unmask IRQ 12 (Slave PIC line 4)
    interrupts_enable_irq(12);
    
    syslog_write("Mouse: PS/2 initialized (IRQ12 unmasked)");
}

void mouse_handle_interrupt(void) {
    uint8_t status = inb(MOUSE_PORT_STATUS);
    
    // Check if there is data to read. If not, we can't do anything.
    if (!(status & 0x01)) return;

    // Read the data. This effectively clears the interrupt.
    uint8_t b = inb(MOUSE_PORT_DATA);

    // If the data didn't come from the mouse (Bit 5 of status), ignore it.
    // However, by reading it above, we ensured the controller isn't stuck.
    if (!(status & 0x20)) return;

    switch(g_mouse_cycle) {
        case 0:
            // Packet Byte 1:
            // Bit 3 must be 1. 
            // Bit 6,7: Overflow (usually 0).
            // Note: Some mice might not set Bit 3 strictly if not fully compliant, 
            // but most standard PS/2 mice do. We'll keep the check for sync.
            if ((b & 0x08) == 0x08) { 
                g_mouse_byte[0] = b;
                g_mouse_cycle++;
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

            // Decode Packet
            // Byte 0: Y_Overflow X_Overflow Y_Sign X_Sign 1 Middle Right Left
            
            // Handle 9-bit signed values
            int dx = (int8_t)g_mouse_byte[1];
            int dy = (int8_t)g_mouse_byte[2];
            
            // Check Overflow bits in byte 0
            bool x_overflow = (g_mouse_byte[0] & 0x40) != 0;
            bool y_overflow = (g_mouse_byte[0] & 0x80) != 0;
            
            if (x_overflow || y_overflow) {
                // Discard erratic movement
                dx = 0;
                dy = 0;
            }

            // Apply Sensitivity
            dx *= g_sensitivity;
            dy *= g_sensitivity;
            
            // Update Position (PS/2 Y is bottom-to-top, screen is top-to-bottom)
            g_mouse_x += dx;
            g_mouse_y -= dy; 

            // Clamp to screen dimensions
            int w = graphics_get_width();
            int h = graphics_get_height();
            
            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_x >= w) g_mouse_x = w - 1;
            
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_y >= h) g_mouse_y = h - 1;

            // Update Buttons
            g_left_btn = (g_mouse_byte[0] & 0x01) != 0;
            g_right_btn = (g_mouse_byte[0] & 0x02) != 0;
            
            // Uncomment for heavy debugging (will slow down system)
            // syslog_write("Mouse: Packet processed");
            break;
    }
}

MouseState mouse_get_state(void) {
    MouseState s;
    // Atomic read would be better, but for a hobby OS this is okay
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
