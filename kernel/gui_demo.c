#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "io.h"
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 8
#define MAX_ICONS 4
#define WIN_CAPTION_H 24
#define TASKBAR_H 36

// --- Windows 7 / Aero Basic Hybrid Theme ---
#define COL_DESKTOP     0xFF2D73A8 // Win7 Default Wallpaper Blue
#define COL_TASKBAR     0xFF18334E // Glassy Dark Blue
#define COL_START_BTN   0xFF1F4E79 // Orb-ish Blue
#define COL_WIN_BORDER  0xFF6B95BD // Aero Basic Blue
#define COL_WIN_TITLE   0xFF000000 // Black Text
#define COL_WIN_BODY    0xFFF0F0F0 // Clean White/Grey
#define COL_BTN_HOVER   0xFF4F81BD
#define COL_WHITE       0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_RED         0xFFE81123 // Close Button Red
#define COL_HIGHLIGHT   0x40FFFFFF // Simulated alpha highlight (just a lighter color logic in drawing)

// --- Structures ---

typedef enum {
    WIN_NONE = 0,
    WIN_WELCOME,
    WIN_ABOUT,
    WIN_CALC,
    WIN_NOTEPAD
} WindowType;

typedef struct {
    int id;
    WindowType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool dragging;
    int drag_off_x;
    int drag_off_y;
    // Z-order is implicit by array index (higher index = on top)
} Window;

typedef struct {
    char label[16];
    int x, y;
    WindowType action_type;
    bool selected;
} Icon;

typedef struct {
    bool open;
    int x, y, w, h;
} StartMenu;

// --- Global State ---
static Window windows[MAX_WINDOWS];
static Icon icons[MAX_ICONS];
static StartMenu start_menu;
static MouseState mouse;
static MouseState prev_mouse;
static int screen_w, screen_h;

// --- Helpers ---

// Move window at index 'idx' to the end of the array (render last = top)
static void bring_to_front(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS - 1) return;
    // If it's already last active? We need to bubble it up.
    // Simple approach: rotate the array entries from idx to end.
    Window temp = windows[idx];
    for (int i = idx; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[MAX_WINDOWS - 1] = temp;
}

static bool point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

// --- IO Helpers ---
static void sys_shutdown(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
}

static void sys_reboot(void) {
    uint8_t temp = 0x02;
    while (temp & 0x02) temp = inb(0x64);
    outb(0x64, 0xFE);
}

// --- Initialization ---

static void init_gui_system(void) {
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    
    // Clear Windows
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id = i;
        windows[i].visible = false;
        windows[i].type = WIN_NONE;
    }

    // Default Window: Welcome
    int w_idx = MAX_WINDOWS - 1; // Topmost
    windows[w_idx].type = WIN_WELCOME;
    windows[w_idx].visible = true;
    windows[w_idx].x = (screen_w - 300) / 2;
    windows[w_idx].y = (screen_h - 200) / 2;
    windows[w_idx].w = 300;
    windows[w_idx].h = 200;
    // Manual strcpy
    const char* t1 = "Welcome";
    for(int i=0; t1[i]; i++) windows[w_idx].title[i] = t1[i];

    // Setup Icons
    icons[0].x = 20; icons[0].y = 20;
    const char* l1 = "My Computer";
    for(int i=0; l1[i]; i++) icons[0].label[i] = l1[i];
    icons[0].action_type = WIN_ABOUT;

    icons[1].x = 20; icons[1].y = 100;
    const char* l2 = "Notepad";
    for(int i=0; l2[i]; i++) icons[1].label[i] = l2[i];
    icons[1].action_type = WIN_NOTEPAD;

    icons[2].x = 20; icons[2].y = 180;
    const char* l3 = "Calculator";
    for(int i=0; l3[i]; i++) icons[2].label[i] = l3[i];
    icons[2].action_type = WIN_CALC;

    // Start Menu
    start_menu.open = false;
    start_menu.w = 160;
    start_menu.h = 220;
    start_menu.x = 0;
    start_menu.y = screen_h - TASKBAR_H - start_menu.h;
}

static void spawn_window(WindowType type) {
    // Check if already open, if so, bring to front and restore
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible && windows[i].type == type) {
            windows[i].minimized = false;
            bring_to_front(i);
            return;
        }
    }

    // Find empty slot (from bottom of stack)
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) {
            slot = i;
            break;
        }
    }
    if (slot == -1) slot = 0; // Overwrite bottom-most if full

    // Initialize
    Window* w = &windows[slot];
    w->visible = true;
    w->minimized = false;
    w->type = type;
    w->w = 300;
    w->h = 200;
    w->x = 50 + (slot * 20);
    w->y = 50 + (slot * 20);
    
    // Set Title
    const char* title = "Window";
    if (type == WIN_ABOUT) title = "System Info";
    if (type == WIN_CALC) title = "Calculator";
    if (type == WIN_NOTEPAD) title = "Notepad";
    
    // Clear title buffer
    for(int i=0; i<32; i++) w->title[i] = 0;
    for(int i=0; title[i] && i<31; i++) w->title[i] = title[i];

    bring_to_front(slot);
}

// --- Rendering ---

static void draw_raised_rect(int x, int y, int w, int h, uint32_t col) {
    graphics_fill_rect(x, y, w, h, col);
    graphics_fill_rect(x, y, w, 1, 0xFFFFFFFF); // Top Light
    graphics_fill_rect(x, y, 1, h, 0xFFFFFFFF); // Left Light
    graphics_fill_rect(x, y+h-1, w, 1, 0xFF000000); // Bot Shadow
    graphics_fill_rect(x+w-1, y, 1, h, 0xFF000000); // Right Shadow
}

static void draw_window_frame(Window* w, bool active) {
    if (!w->visible || w->minimized) return;

    uint32_t border_col = active ? COL_WIN_BORDER : 0xFF888888;
    
    // 1. Frame/Border with Shadow offset
    graphics_fill_rect(w->x + 5, w->y + 5, w->w, w->h, 0x40000000); // Pseudo Shadow
    graphics_fill_rect(w->x, w->y, w->w, w->h, border_col);
    
    // 2. Title Bar
    // Gradient simulation: Top half lighter
    graphics_fill_rect(w->x + 2, w->y + 2, w->w - 4, WIN_CAPTION_H, border_col);
    
    graphics_draw_string_scaled(w->x + 8, w->y + 6, w->title, COL_BLACK, border_col, 1);

    // 3. Controls (Min / Close)
    int btn_w = 20;
    int btn_h = 18;
    int close_x = w->x + w->w - btn_w - 4;
    int min_x = close_x - btn_w - 2;
    int btn_y = w->y + 4;

    // Close [X] - Red
    graphics_fill_rect(close_x, btn_y, btn_w, btn_h, COL_RED);
    graphics_draw_char(close_x + 6, btn_y + 5, 'X', COL_WHITE, COL_RED);

    // Min [_] - Border Color Darkened
    graphics_fill_rect(min_x, btn_y, btn_w, btn_h, 0xFF507291);
    graphics_draw_char(min_x + 6, btn_y + 5, '_', COL_WHITE, 0xFF507291);

    // 4. Client Area
    graphics_fill_rect(w->x + 4, w->y + WIN_CAPTION_H, w->w - 8, w->h - WIN_CAPTION_H - 4, COL_WIN_BODY);

    // 5. Content Stubs
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    uint32_t bg = COL_WIN_BODY;
    uint32_t fg = COL_BLACK;

    if (w->type == WIN_WELCOME) {
        graphics_draw_string_scaled(cx, cy, "Welcome to Nostalux OS", fg, bg, 1);
        graphics_draw_string_scaled(cx, cy+20, "This is a GUI Demo.", fg, bg, 1);
        graphics_draw_string_scaled(cx, cy+40, "Drag windows by the title.", fg, bg, 1);
    } else if (w->type == WIN_CALC) {
        draw_raised_rect(cx, cy, 150, 30, COL_WHITE);
        graphics_draw_string_scaled(cx+140, cy+10, "0", fg, COL_WHITE, 1);
        // Mock Buttons
        for(int i=0; i<3; i++) {
            for(int j=0; j<3; j++) {
                draw_raised_rect(cx + j*40, cy + 40 + i*30, 35, 25, 0xFFDDDDDD);
            }
        }
    } else if (w->type == WIN_NOTEPAD) {
        graphics_fill_rect(cx, cy, w->w - 20, w->h - 50, COL_WHITE);
        graphics_draw_string_scaled(cx+2, cy+2, "Type something...", 0xFF888888, COL_WHITE, 1);
    }
}

static void draw_icon(Icon* icon) {
    // Simple "File" shape
    int ix = icon->x;
    int iy = icon->y;
    
    // Icon Body
    graphics_fill_rect(ix + 10, iy, 30, 40, 0xFFF4C842); // Yellow folder-ish
    graphics_fill_rect(ix + 10, iy, 30, 2, 0xFFFFFFFF);
    
    // Label Background (if selected)
    if (icon->selected) {
        int len = kstrlen(icon->label);
        graphics_fill_rect(ix, iy + 45, len * 8, 10, 0xFF0000AA);
        graphics_draw_string_scaled(ix, iy + 45, icon->label, COL_WHITE, 0xFF0000AA, 1);
    } else {
        // Shadow text effect
        graphics_draw_string_scaled(ix + 1, iy + 46, icon->label, 0xFF000000, COL_DESKTOP, 1); // Shadow
        graphics_draw_string_scaled(ix, iy + 45, icon->label, COL_WHITE, COL_DESKTOP, 1);
    }
}

static void draw_taskbar_ui(void) {
    // Bar
    int y = screen_h - TASKBAR_H;
    graphics_fill_rect(0, y, screen_w, TASKBAR_H, COL_TASKBAR);
    // Glassy highlight top
    graphics_fill_rect(0, y, screen_w, 1, 0xFF607080);

    // Start Button (Round-ish Rect)
    draw_raised_rect(2, y + 2, 50, TASKBAR_H - 4, COL_START_BTN);
    graphics_draw_string_scaled(8, y + 12, "Start", COL_WHITE, COL_START_BTN, 1);

    // Taskbar Buttons for Windows
    int btn_x = 60;
    int btn_w = 100;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible) {
            uint32_t col = windows[i].minimized ? COL_TASKBAR : 0xFF2D4A6B;
            draw_raised_rect(btn_x, y + 2, btn_w, TASKBAR_H - 4, col);
            
            // Truncate title if needed? Just draw simply
            graphics_draw_string_scaled(btn_x + 5, y + 12, windows[i].title, COL_WHITE, col, 1);
            
            btn_x += btn_w + 2;
        }
    }

    // Clock
    graphics_draw_string_scaled(screen_w - 50, y + 12, "12:00", COL_WHITE, COL_TASKBAR, 1);
}

static void draw_start_menu_ui(void) {
    if (!start_menu.open) return;
    
    // Background
    draw_raised_rect(start_menu.x, start_menu.y, start_menu.w, start_menu.h, 0xFFF0F0F0);
    
    // Left Pane (White)
    graphics_fill_rect(start_menu.x + 2, start_menu.y + 2, 100, start_menu.h - 40, COL_WHITE);
    
    // Right Pane (Light Blue)
    graphics_fill_rect(start_menu.x + 102, start_menu.y + 2, start_menu.w - 104, start_menu.h - 40, 0xFFD0E0F0);

    // Items
    int iy = start_menu.y + 10;
    graphics_draw_string_scaled(start_menu.x + 10, iy, "Notepad", COL_BLACK, COL_WHITE, 1);
    graphics_draw_string_scaled(start_menu.x + 10, iy + 20, "Calc", COL_BLACK, COL_WHITE, 1);
    graphics_draw_string_scaled(start_menu.x + 10, iy + 40, "Info", COL_BLACK, COL_WHITE, 1);

    // Bottom Bar (Shutdown)
    int by = start_menu.y + start_menu.h - 36;
    graphics_fill_rect(start_menu.x + 2, by, start_menu.w - 4, 34, 0xFF303030);
    
    // Shutdown Button
    draw_raised_rect(start_menu.x + start_menu.w - 80, by + 6, 70, 22, 0xFFE81123); // Red
    graphics_draw_string_scaled(start_menu.x + start_menu.w - 70, by + 12, "Off", COL_WHITE, 0xFFE81123, 1);
}

static void draw_cursor_sprite(void) {
    int x = mouse.x;
    int y = mouse.y;
    // Standard Arrow
    uint32_t white = 0xFFFFFFFF;
    uint32_t black = 0xFF000000;
    
    graphics_fill_rect(x, y, 2, 14, black);
    graphics_fill_rect(x, y, 12, 2, black);
    graphics_fill_rect(x+2, y+2, 8, 10, white);
    graphics_fill_rect(x+2, y+12, 2, 2, black); // Tip
}

// --- Logic Core ---

static void handle_input_logic(void) {
    // 1. Keyboard (Q to quit)
    char c = keyboard_poll_char();
    if (c == 'q' || c == 'Q') {
        // Signal exit (we'll check this in main loop)
        // For now, just rely on main loop check or explicit shutdown
    }

    // 2. Mouse Button Logic (Click vs Drag)
    bool click_event = (mouse.left_button && !prev_mouse.left_button);
    bool release_event = (!mouse.left_button && prev_mouse.left_button);

    if (click_event) {
        bool hit_handled = false;

        // A. Start Menu Items (if open)
        if (start_menu.open && point_in_rect(mouse.x, mouse.y, start_menu.x, start_menu.y, start_menu.w, start_menu.h)) {
            hit_handled = true;
            // Check Shutdown
            if (point_in_rect(mouse.x, mouse.y, start_menu.x + start_menu.w - 80, start_menu.y + start_menu.h - 30, 70, 22)) {
                sys_shutdown();
            }
            // Check Apps
            if (mouse.y < start_menu.y + 30) spawn_window(WIN_NOTEPAD);
            else if (mouse.y < start_menu.y + 50) spawn_window(WIN_CALC);
            else if (mouse.y < start_menu.y + 70) spawn_window(WIN_ABOUT);
            
            start_menu.open = false; // Auto close on click
        }
        else if (start_menu.open) {
            start_menu.open = false; // Clicked outside
        }

        // B. Start Button
        if (!hit_handled && point_in_rect(mouse.x, mouse.y, 0, screen_h - TASKBAR_H, 55, TASKBAR_H)) {
            start_menu.open = !start_menu.open;
            hit_handled = true;
        }

        // C. Taskbar Buttons
        if (!hit_handled && mouse.y >= screen_h - TASKBAR_H) {
            int btn_x = 60;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].visible) {
                    if (point_in_rect(mouse.x, mouse.y, btn_x, screen_h - TASKBAR_H, 100, TASKBAR_H)) {
                        // Toggle Minimize
                        if (i == MAX_WINDOWS - 1 && !windows[i].minimized) {
                            windows[i].minimized = true;
                        } else {
                            windows[i].minimized = false;
                            bring_to_front(i);
                        }
                        hit_handled = true;
                        break;
                    }
                    btn_x += 102;
                }
            }
            hit_handled = true; // Consumed by taskbar bg
        }

        // D. Windows (Top to Bottom)
        if (!hit_handled) {
            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                Window* w = &windows[i];
                if (!w->visible || w->minimized) continue;

                // Hit Window
                if (point_in_rect(mouse.x, mouse.y, w->x, w->y, w->w, w->h)) {
                    bring_to_front(i); // Focus
                    
                    // Check Close
                    if (point_in_rect(mouse.x, mouse.y, w->x + w->w - 26, w->y + 4, 20, 18)) {
                        w->visible = false;
                    }
                    // Check Minimize
                    else if (point_in_rect(mouse.x, mouse.y, w->x + w->w - 50, w->y + 4, 20, 18)) {
                        w->minimized = true;
                    }
                    // Check Drag (Title Bar)
                    else if (mouse.y < w->y + WIN_CAPTION_H) {
                        // We must get the pointer to the NEW location after bring_to_front
                        // Since we called bring_to_front(i), this window is now at MAX_WINDOWS-1
                        windows[MAX_WINDOWS-1].dragging = true;
                        windows[MAX_WINDOWS-1].drag_off_x = mouse.x - w->x;
                        windows[MAX_WINDOWS-1].drag_off_y = mouse.y - w->y;
                    }
                    
                    hit_handled = true;
                    break;
                }
            }
        }

        // E. Desktop Icons
        if (!hit_handled) {
            for (int i = 0; i < MAX_ICONS; i++) {
                // Simple hit box
                if (point_in_rect(mouse.x, mouse.y, icons[i].x, icons[i].y, 50, 50)) {
                    icons[i].selected = true;
                    // Double click simulation? For now single click launch
                    spawn_window(icons[i].action_type);
                } else {
                    icons[i].selected = false;
                }
            }
        }
    }

    if (release_event) {
        for (int i = 0; i < MAX_WINDOWS; i++) windows[i].dragging = false;
    }

    // Drag Logic
    if (mouse.left_button) {
        // Only the topmost window can be dragging
        Window* top = &windows[MAX_WINDOWS - 1];
        if (top->visible && top->dragging) {
            top->x = mouse.x - top->drag_off_x;
            top->y = mouse.y - top->drag_off_y;
        }
    }
}

void gui_demo_run(void) {
    syslog_write("GUI: Starting Desktop Environment...");
    
    mouse_init();
    init_gui_system();

    bool running = true;
    
    while (running) {
        // Check Quit Key
        // (We need a raw check or assume 'q' from poll)
        // char c = keyboard_poll_char(); 
        // if (c == 'q') running = false;

        // Update Inputs
        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        handle_input_logic();

        // Draw Frame
        // 1. Desktop
        graphics_fill_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
        
        // 2. Icons
        for (int i = 0; i < 3; i++) draw_icon(&icons[i]);

        // 3. Windows (Bottom to Top)
        for (int i = 0; i < MAX_WINDOWS; i++) {
            // Active window is last, so it draws on top
            bool is_active = (i == MAX_WINDOWS - 1);
            draw_window_frame(&windows[i], is_active);
        }

        // 4. Shells
        draw_taskbar_ui();
        draw_start_menu_ui();
        
        // 5. Cursor
        draw_cursor_sprite();

        // Loop delay
        timer_wait(1);
    }
}
