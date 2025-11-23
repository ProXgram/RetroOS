#include "banner.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "terminal.h"
#include "background.h"
#include "kstring.h"

// Helper to draw a string at specific pixel coordinates directly to the framebuffer
static void draw_string_px(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    int cur_x = x;
    while (*str) {
        graphics_draw_char(cur_x, y, *str, fg, bg);
        cur_x += 8; // 8x8 font spacing
        str++;
    }
}

void banner_run(void) {
    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();
    
    const char* text = "NOSTALUX OS";
    int text_len_px = (int)kstrlen(text) * 8; 
    int text_h_px = 8;
    
    // Start in center
    int x = width / 2 - text_len_px / 2;
    int y = height / 2 - text_h_px / 2;
    
    // Velocity
    int dx = 3;
    int dy = 3;
    
    // Bouncing colors
    uint32_t colors[] = {
        0xFFFF0000, // Red
        0xFF00FF00, // Green
        0xFF0000FF, // Blue
        0xFFFFFF00, // Yellow
        0xFF00FFFF, // Cyan
        0xFFFF00FF, // Magenta
        0xFFFFFFFF  // White
    };
    int color_idx = 0;
    const int color_count = sizeof(colors) / sizeof(colors[0]);
    uint32_t current_color = colors[0];
    uint32_t bg_color = 0xFF000000; // Black

    // Clear screen initially
    graphics_fill_rect(0, 0, width, height, bg_color);
    
    while (true) {
        // Exit on keypress
        if (keyboard_poll_char()) {
            break;
        }
        
        // Erase old text position
        graphics_fill_rect(x, y, text_len_px, text_h_px, bg_color);
        
        // Update position
        x += dx;
        y += dy;
        
        // Collision detection and bouncing
        bool bounced = false;
        
        if (x <= 0) {
            x = 0;
            dx = -dx;
            bounced = true;
        } else if (x + text_len_px >= width) {
            x = width - text_len_px;
            dx = -dx;
            bounced = true;
        }
        
        if (y <= 0) {
            y = 0;
            dy = -dy;
            bounced = true;
        } else if (y + text_h_px >= height) {
            y = height - text_h_px;
            dy = -dy;
            bounced = true;
        }
        
        // Change color on bounce
        if (bounced) {
            color_idx = (color_idx + 1) % color_count;
            current_color = colors[color_idx];
        }
        
        // Draw text at new position
        draw_string_px(x, y, text, current_color, bg_color);
        
        // Wait ~20ms (2 ticks at 100Hz)
        timer_wait(2);
    }
    
    // Restore the standard UI background before returning to shell
    background_render();
}
