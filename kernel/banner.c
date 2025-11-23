#include "banner.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "terminal.h"
#include "background.h"
#include "kstring.h"
#include "syslog.h"

// Helper to draw a string at specific pixel coordinates directly to the framebuffer
static void draw_string_px(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    int cur_x = x;
    while (*str) {
        graphics_draw_char(cur_x, y, *str, fg, bg);
        cur_x += 8; // 8x8 font spacing
        str++;
    }
}

// The big banner ASCII art
static const char* BIG_BANNER[] = {
    " _   _           _        _             ____   _____                           ",
    "| \\ | |         | |      | |           / __ \\ / ____|                          ",
    "|  \\| | ___  ___| |_ __ _| |_   ___  _| |  | | (___   NOSTALUX                 ",
    "| . ` |/ _ \\ / __| __/ _` | | | | \\ \\/ / |  | |\\___ \\                         ",
    "| |\\  | (_) \\__ \\ || (_| | | |_| |>  <| |__| |____) |                        ",
    "|_| \\_|\\___/|___/\\__\\__,_|_|\\__,_/_/\\_\\\\____/|_____/                         ",
};
static const int BANNER_LINE_COUNT = 6;

static void draw_big_banner(int x, int y, uint32_t fg, uint32_t bg) {
    for (int i = 0; i < BANNER_LINE_COUNT; i++) {
        draw_string_px(x, y + (i * 8), BIG_BANNER[i], fg, bg);
    }
}

void banner_run(void) {
    syslog_write("Banner: Starting big animation...");
    
    // Flush pending input
    for (int i = 0; i < 10; i++) {
        if (keyboard_poll_char() == 0) break;
    }

    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();
    
    if (width == 0 || height == 0) return;

    // Calculate dimensions
    int banner_w_chars = 0;
    for (int i = 0; i < BANNER_LINE_COUNT; i++) {
        int len = (int)kstrlen(BIG_BANNER[i]);
        if (len > banner_w_chars) banner_w_chars = len;
    }
    
    int obj_w = banner_w_chars * 8;
    int obj_h = BANNER_LINE_COUNT * 8;
    
    // Start in center
    int x = width / 2 - obj_w / 2;
    int y = height / 2 - obj_h / 2;
    
    // Velocity
    int dx = 2;
    int dy = 2;
    
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

    // Clear screen
    graphics_fill_rect(0, 0, width, height, bg_color);
    
    while (true) {
        if (keyboard_poll_char() != 0) break;
        
        // Erase old position
        graphics_fill_rect(x, y, obj_w, obj_h, bg_color);
        
        // Update
        x += dx;
        y += dy;
        
        // Bounce
        bool bounced = false;
        
        if (x <= 0) {
            x = 0;
            dx = -dx;
            bounced = true;
        } else if (x + obj_w >= width) {
            x = width - obj_w;
            dx = -dx;
            bounced = true;
        }
        
        if (y <= 0) {
            y = 0;
            dy = -dy;
            bounced = true;
        } else if (y + obj_h >= height) {
            y = height - obj_h;
            dy = -dy;
            bounced = true;
        }
        
        if (bounced) {
            color_idx = (color_idx + 1) % color_count;
            current_color = colors[color_idx];
        }
        
        // Draw
        draw_big_banner(x, y, current_color, bg_color);
        
        // Wait
        timer_wait(2);
    }
    
    background_render();
}
