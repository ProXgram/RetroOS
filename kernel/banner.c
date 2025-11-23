#include "banner.h"
#include "graphics.h"
#include "keyboard.h"
#include "timer.h"
#include "terminal.h"
#include "background.h"
#include "kstring.h"
#include "syslog.h"
#include "sound.h"

// Helper to draw a string at specific pixel coordinates directly to the framebuffer
static void draw_string_px(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    int cur_x = x;
    while (*str) {
        graphics_draw_char(cur_x, y, *str, fg, bg);
        cur_x += 8; // 8x8 font spacing
        str++;
    }
}

// The big banner ASCII art (used by banner_run)
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

// Screensaver Mode
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

// Cinematic Boot Mode
void banner_boot_splash(void) {
    int width = graphics_get_width();
    int height = graphics_get_height();
    
    // Animation configuration
    int scale_title = 6;
    int scale_sub = 2;
    const char* title_text = "NOSTALUX";
    const char* sub_text = "OPERATING SYSTEM";
    
    // Calculate final centered positions
    int title_px_w = (int)kstrlen(title_text) * 8 * scale_title;
    int sub_px_w = (int)kstrlen(sub_text) * 8 * scale_sub;
    
    int final_title_x = (width - title_px_w) / 2;
    int final_sub_x = (width - sub_px_w) / 2;
    
    int title_y = (height / 2) - (8 * scale_title) / 2 - 20;
    int sub_y = title_y + (8 * scale_title) + 20;

    // Clear screen to black initially
    graphics_fill_rect(0, 0, width, height, 0xFF000000);

    // Animation Loop (60 frames ~ 1.2 seconds if 20ms delay)
    const int TOTAL_FRAMES = 60;
    
    for (int frame = 0; frame <= TOTAL_FRAMES; frame++) {
        // Simple linear progress 0.0 to 1.0
        float t = (float)frame / (float)TOTAL_FRAMES;
        
        // Easing: ease-out cubic
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        // Slide Title from Left
        int start_title_x = -title_px_w;
        int current_title_x = start_title_x + (int)((final_title_x - start_title_x) * ease);
        
        // Slide Subtitle from Right
        int start_sub_x = width;
        int current_sub_x = start_sub_x + (int)((final_sub_x - start_sub_x) * ease);

        // Redraw (clear only the band where text is to optimize?)
        // For simplicity and correctness, clear screen (black)
        graphics_fill_rect(0, 0, width, height, 0xFF000000);

        // Draw Text
        graphics_draw_string_scaled(current_title_x, title_y, title_text, 0xFF00FFFF, 0, scale_title);
        graphics_draw_string_scaled(current_sub_x, sub_y, sub_text, 0xFFFFFF00, 0, scale_sub);

        // Sound effects synced to animation
        if (frame == 10) sound_beep(220, 1);
        if (frame == 30) sound_beep(330, 1);
        if (frame == 50) sound_beep(440, 1);

        // Allow user to skip with any key
        if (keyboard_poll_char() != 0) return;

        timer_wait(2); // 20ms
    }

    // Play startup chime
    sound_beep(523, 10); // C5
    sound_beep(659, 10); // E5
    sound_beep(784, 20); // G5

    // Hold the final frame for a moment
    for (int i = 0; i < 50; i++) { // ~1 second
        if (keyboard_poll_char() != 0) return;
        timer_wait(2);
    }
    
    // Clear screen before handing off to shell
    graphics_fill_rect(0, 0, width, height, 0xFF000000);
}
