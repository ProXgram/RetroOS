#include "background.h"
#include <stddef.h>
#include "os_info.h"
#include "syslog.h"
#include "terminal.h"
#include "kstring.h"
#include "graphics.h"
#include "system.h"

// Reserve top 12 rows for the graphical banner to make it BIG
#define HEADER_ROWS 12
#define BG_COLOR_HEX 0xFF0000AA // Standard VGA Blue in ARGB

void background_render(void) {
    uint8_t original_fg = 0;
    uint8_t original_bg = 0;
    terminal_getcolors(&original_fg, &original_bg);

    terminal_begin_batch();
    terminal_setcolors(0x0F, 0x01); // White on Blue
    terminal_clear(); // Resets row to 0
    
    // Draw Borders Text (Top and Bottom of the header area)
    const char* border = "================================================================================";
    terminal_write_at(0, 0, border, 0x0F, 0x01);
    terminal_write_at(HEADER_ROWS - 1, 0, border, 0x0F, 0x01);
    
    // Move cursor below the header so the shell prompt appears in the right place
    // We do this by printing newlines to push the cursor down past the reserved area
    for(int i = 0; i < HEADER_ROWS; i++) {
        terminal_newline();
    }

    terminal_setcolors(original_fg, original_bg);
    terminal_end_batch();

    syslog_write("UI: background refreshed with GUI header");
}

void background_animate(void) {
    // This runs in the idle loop.
    // We will draw High-Res Graphics in the area between row 0 and row HEADER_ROWS-1.
    // Row 0 is y=0..7. Row 11 is y=88..95.
    // Usable pixel area: y=8 to y=(HEADER_ROWS-1)*8.
    
    static int tick = 0;
    static int x_pos = 10;
    static int direction = 2;
    static uint32_t logo_color = 0xFFFFFF00;
    
    int width = graphics_get_width();
    
    // 1. Clear the header area (pixels) to Blue to erase previous frame
    // We calculate the pixel boundaries of the "empty" space inside the text borders
    int y_start = 8;
    int y_end = (HEADER_ROWS - 1) * 8;
    int height = y_end - y_start;
    
    if (height <= 0) return;

    // Fill the background of the header with blue to wipe old text/graphics
    graphics_fill_rect(0, y_start, width, height, BG_COLOR_HEX);
    
    // 2. Animate "NOSTALUX" (Big and Bouncing)
    const char* text = "NOSTALUX";
    int scale = 6; // Very Big Text
    int text_w = kstrlen(text) * 8 * scale;
    int text_h = 8 * scale;
    
    // Center Y position
    int y_pos = y_start + (height - text_h) / 2;
    
    // Bounce X Logic
    int max_x = width - text_w;
    if (max_x < 0) max_x = 0;
    
    if (x_pos >= max_x) {
        x_pos = max_x;
        if (direction > 0) direction = -direction;
    }
    if (x_pos <= 0) {
        x_pos = 0;
        if (direction < 0) direction = -direction;
    }
    
    x_pos += direction;
    
    // Color Cycling Effect
    if (tick % 5 == 0) {
        // Simple rainbow cycle
        uint32_t colors[] = {0xFFFFFF00, 0xFF00FFFF, 0xFFFF00FF, 0xFF00FF00, 0xFFFFFFFF, 0xFFFF8800};
        logo_color = colors[(tick / 5) % 6];
    }
    
    // Draw the Logo directly to framebuffer
    graphics_draw_string_scaled(x_pos, y_pos, text, logo_color, BG_COLOR_HEX, scale);
    
    // 3. Draw subtitle centered at the bottom of the header
    const char* sub = "The Future of Retro Computing";
    int sub_scale = 1;
    int sub_w = kstrlen(sub) * 8 * sub_scale;
    graphics_draw_string_scaled((width - sub_w)/2, y_end - 12, sub, 0xFFAAAAAA, BG_COLOR_HEX, sub_scale);
    
    tick++;
}
