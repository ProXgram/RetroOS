#include "gui_demo.h"
#include "gui_defs.h" // Helper
#include "graphics.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "syslog.h"

void gui_demo_run(void) {
    syslog_write("GUI: Loading Window Manager...");
    
    mouse_init();
    wm_init(graphics_get_width(), graphics_get_height());
    
    // Open initial window
    wm_create_window(APP_WELCOME, "Welcome", 300, 150);

    bool running = true;
    MouseState prev_mouse = mouse_get_state();

    while (running) {
        // 1. Keyboard
        char c = keyboard_poll_char();
        if (c == 27) running = false; // Esc to quit
        if (c) wm_handle_key(c);

        // 2. Mouse
        MouseState m = mouse_get_state();
        bool clicked = (m.left_button && !prev_mouse.left_button);
        bool released = (!m.left_button && prev_mouse.left_button);

        if (clicked) wm_handle_click(m.x, m.y);
        else if (m.left_button) wm_handle_drag(m.x, m.y);
        
        if (released) wm_handle_release();

        prev_mouse = m;

        // 3. Render
        wm_render_all();
        
        // Draw Cursor (Manual overlay)
        int cx = m.x, cy = m.y;
        graphics_fill_rect(cx, cy, 2, 12, 0xFF000000);
        graphics_fill_rect(cx, cy, 12, 2, 0xFF000000);
        graphics_fill_rect(cx+2, cy+2, 8, 8, 0xFFFFFFFF);

        // 4. Wait
        timer_wait(1);
    }
    
    syslog_write("GUI: Unloaded.");
}
