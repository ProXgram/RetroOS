#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "io.h"
#include "kstdio.h"
#include "fs.h"
#include "system.h"
#include "heap.h"  // REQUIRED for kmalloc/kfree
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 16
#define WIN_CAPTION_H 28
#define TASKBAR_H 34

// --- Colors ---
#define COL_DESKTOP_TOP 0xFF2D73A8
#define COL_DESKTOP_BOT 0xFF103050
#define COL_TASKBAR     0xFF18334E
#define COL_START_BTN   0xFF1F4E79
#define COL_START_HOVER 0xFF3465A4
#define COL_WIN_TITLE_1 0xFF0058EE
#define COL_WIN_TITLE_2 0xFF002488
#define COL_WIN_INACT_1 0xFF888888
#define COL_WIN_INACT_2 0xFF666666
#define COL_WIN_BODY    0xFFECE9D8
#define COL_BTN_HOVER   0xFF4F81BD
#define COL_BTN_PRESS   0xFF2F518D
#define COL_WHITE       0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_RED         0xFFE81123
#define COL_RED_HOVER   0xFFFF4444
#define COL_GRAY        0xFFCCCCCC

// --- Types ---
typedef enum { APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC, APP_FILES } AppType;

typedef struct {
    int current_val;
    int accumulator;
    char op;
    bool new_entry;
} CalcState;

typedef struct {
    char buffer[256];
    int length;
} NotepadState;

typedef struct {
    int selected_index;
    int scroll_offset;
} FileManagerState;

typedef struct {
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    
    // State flags
    bool visible;
    bool minimized;
    bool maximized;
    bool focused;
    bool dragging;
    
    // For dragging/restoring
    int drag_off_x, drag_off_y;
    int restore_x, restore_y, restore_w, restore_h;

    union { 
        CalcState calc; 
        NotepadState notepad; 
        FileManagerState files;
    } state;
} Window;

// --- State ---
// Array of pointers to windows.
// Windows are allocated on the heap via kmalloc.
// Initialized to NULL by BSS section automatically.
static Window* windows[MAX_WINDOWS];

static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;

// --- RTC Helper ---
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, (uint8_t)reg);
    return inb(CMOS_DATA);
}

static void get_time_string(char* buf) {
    // Simple timeout to prevent hanging if RTC is busy
    int timeout = 1000;
    while ((get_rtc_register(0x0A) & 0x80) && timeout-- > 0);
    
    uint8_t min = get_rtc_register(0x02);
    uint8_t hour = get_rtc_register(0x04);
    
    // Convert BCD to binary
    min = (min & 0x0F) + ((min / 16) * 10);
    hour = ((hour & 0x0F) + ((hour / 16) * 10));
    
    // Simple formatting
    buf[0] = '0' + (hour / 10);
    buf[1] = '0' + (hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (min / 10);
    buf[4] = '0' + (min % 10);
    buf[5] = 0;
}

// --- Helpers ---
static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void str_copy(char* dest, const char* src) {
    int i = 0;
    while (src[i] && i < 31) { dest[i] = src[i]; i++; }
    dest[i] = 0;
}

static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    bool neg = v < 0;
    if (neg) v = -v;
    int i = 0; char tmp[16];
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

// --- Window Management (Pointer based) ---

// Brings the window at 'index' to the front (end of array)
// This just swaps pointers, it does not copy structs!
static void focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return;
    if (windows[index] == NULL) return;

    Window* target = windows[index];
    
    // Shift everything after 'index' down by one slot
    // until we hit the end or a NULL
    int i = index;
    while (i < MAX_WINDOWS - 1 && windows[i+1] != NULL) {
        windows[i] = windows[i+1];
        i++;
    }
    
    // Place target at the top
    windows[i] = target;

    // Update focus flags
    for(int j=0; j<MAX_WINDOWS; j++) {
        if (windows[j]) {
            windows[j]->focused = (windows[j] == target);
            if (windows[j]->focused) windows[j]->minimized = false;
        }
    }
}

// Removes window and frees memory
static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    
    kfree(windows[index]);
    windows[index] = NULL;

    // Shift remaining windows down to fill gap
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[MAX_WINDOWS - 1] = NULL;
}

static void create_window(AppType type, const char* title, int w, int h) {
    // 1. Find first empty slot
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] == NULL) {
            slot = i;
            break;
        }
    }

    // 2. If full, close the bottom-most window (index 0)
    if (slot == -1) {
        close_window(0);
        slot = MAX_WINDOWS - 1; // The last slot is now free after shifting
    }

    // 3. Allocate new window
    Window* win = (Window*)kmalloc(sizeof(Window));
    if (!win) {
        syslog_write("GUI: Memory allocation failed for new window");
        return;
    }

    // 4. Initialize
    win->id = slot;
    win->type = type;
    str_copy(win->title, title);
    win->w = w; win->h = h;
    
    static int cascade = 0;
    win->x = 40 + cascade;
    win->y = 40 + cascade;
    cascade = (cascade + 20) % 160;

    // Clamp to screen
    if (win->x + w > screen_w) win->x = 20;
    if (win->y + h > screen_h - TASKBAR_H) win->y = 20;

    win->visible = true;
    win->minimized = false;
    win->maximized = false;
    win->dragging = false;
    win->focused = true;

    // App-specific init
    if (type == APP_NOTEPAD) {
        win->state.notepad.length = 0;
        win->state.notepad.buffer[0] = 0;
    } else if (type == APP_CALC) {
        win->state.calc.current_val = 0;
        win->state.calc.new_entry = true;
    } else if (type == APP_FILES) {
        win->state.files.selected_index = -1;
        win->state.files.scroll_offset = 0;
    }

    // 5. Assign to array and focus
    windows[slot] = win;
    focus_window(slot);
}

// --- Drawing ---

static void draw_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i=0; i<h; i++) {
        uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
        uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
        
        // Fixed point math roughly
        uint8_t r = r1 + ((int)(r2 - r1) * i) / h;
        uint8_t g = g1 + ((int)(g2 - g1) * i) / h;
        uint8_t b = b1 + ((int)(b2 - b1) * i) / h;
        
        uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;
        graphics_fill_rect(x, y+i, w, 1, col);
    }
}

static void draw_bevel_rect(int x, int y, int w, int h, uint32_t fill, bool sunk) {
    graphics_fill_rect(x, y, w, h, fill);
    uint32_t tl = sunk ? 0xFF555555 : 0xFFFFFFFF; 
    uint32_t br = sunk ? 0xFFFFFFFF : 0xFF555555; 
    graphics_fill_rect(x, y, w, 1, tl);      
    graphics_fill_rect(x, y, 1, h, tl);      
    graphics_fill_rect(x, y+h-1, w, 1, br);  
    graphics_fill_rect(x+w-1, y, 1, h, br);  
}

static void render_notepad(Window* w, int cx, int cy) {
    draw_bevel_rect(cx+2, cy+2, w->w-4, w->h-WIN_CAPTION_H-4, COL_WHITE, true);
    graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
    // Blinking cursor
    if ((timer_get_ticks() / 15) % 2) {
        int cur_x = cx + 6 + (w->state.notepad.length * 8);
        graphics_fill_rect(cur_x, cy+6, 2, 10, COL_BLACK);
    }
}

static void render_calc(Window* w, int cx, int cy) {
    char buf[16];
    int_to_str(w->state.calc.current_val, buf);
    draw_bevel_rect(cx+10, cy+10, w->w-20, 30, COL_WHITE, true);
    int text_w = kstrlen(buf) * 8 * 2;
    graphics_draw_string_scaled(cx+w->w-20-text_w, cy+15, buf, COL_BLACK, COL_WHITE, 2);

    const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    int start_x = cx+10;
    int start_y = cy+50;
    for(int i=0; i<16; i++) {
        int r = i/4; int c = i%4;
        int b_x = start_x + c*40;
        int b_y = start_y + r*35;
        bool hover = rect_contains(b_x, b_y, 35, 30, mouse.x, mouse.y);
        bool press = hover && mouse.left_button;
        uint32_t btn_col = press ? COL_BTN_PRESS : (hover ? COL_BTN_HOVER : 0xFFDDDDDD);
        draw_bevel_rect(b_x, b_y, 35, 30, btn_col, press);
        int tx = b_x + 12; int ty = b_y + 10;
        if(press) { tx++; ty++; }
        graphics_draw_char(tx, ty, btns[i][0], COL_BLACK, btn_col);
    }
}

static void render_file_manager(Window* w, int cx, int cy) {
    int cw = w->w - 8;
    int ch = w->h - WIN_CAPTION_H - 8;
    draw_bevel_rect(cx, cy, cw, ch, COL_WHITE, true);
    graphics_fill_rect(cx+1, cy+1, cw-2, 20, 0xFFEEEEEE);
    graphics_draw_string_scaled(cx+5, cy+7, "Name", COL_BLACK, 0xFFEEEEEE, 1);
    graphics_draw_string_scaled(cx+cw-80, cy+7, "Size", COL_BLACK, 0xFFEEEEEE, 1);
    graphics_fill_rect(cx+1, cy+21, cw-2, 1, 0xFFAAAAAA);

    size_t count = fs_file_count();
    int y_off = 24;
    for (size_t i = 0; i < count; i++) {
        const struct fs_file* f = fs_file_at(i);
        if (!f) continue;
        int row_y = cy + y_off;
        int row_h = 18;
        if (row_y + row_h > cy + ch) break;

        bool selected = ((int)i == w->state.files.selected_index);
        if (selected) graphics_fill_rect(cx+2, row_y, cw-4, row_h, 0xFF316AC5);
        uint32_t txt_col = selected ? COL_WHITE : COL_BLACK;
        uint32_t bg_col = selected ? 0xFF316AC5 : COL_WHITE;

        graphics_fill_rect(cx+4, row_y+2, 12, 12, 0xFFFFCC00);
        graphics_draw_string_scaled(cx+20, row_y+4, f->name, txt_col, bg_col, 1);
        char size_buf[16]; int_to_str(f->size, size_buf);
        graphics_draw_string_scaled(cx+cw-80, row_y+4, size_buf, txt_col, bg_col, 1);
        graphics_draw_string_scaled(cx+cw-40, row_y+4, "B", txt_col, bg_col, 1);
        y_off += row_h;
    }
}

static void render_system_info(Window* w, int cx, int cy) {
    (void)w;
    graphics_draw_string_scaled(cx+20, cy+20, "NostaluxOS v1.0", COL_BLACK, COL_WIN_BODY, 2);
    const struct system_profile* p = system_profile_info();
    char mem[32]; int_to_str(p->memory_total_kb / 1024, mem);
    char res[32]; int_to_str(screen_w, res);
    graphics_draw_string_scaled(cx+20, cy+60, "Memory: ", 0xFF555555, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+90, cy+60, mem, COL_BLACK, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+120, cy+60, "MB", COL_BLACK, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+20, cy+80, "Display:", 0xFF555555, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+90, cy+80, res, COL_BLACK, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+130, cy+80, "px", COL_BLACK, COL_WIN_BODY, 1);
}

static void render_window(Window* w) {
    if (!w || !w->visible || w->minimized) return;

    // Shadow & Body
    if (!w->maximized) graphics_fill_rect(w->x + 4, w->y + 4, w->w, w->h, 0x50000000);
    graphics_fill_rect(w->x, w->y, w->w, w->h, COL_WIN_BODY);
    
    // Title Bar
    uint32_t t1 = w->focused ? COL_WIN_TITLE_1 : COL_WIN_INACT_1;
    uint32_t t2 = w->focused ? COL_WIN_TITLE_2 : COL_WIN_INACT_2;
    draw_gradient_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H, t1, t2);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_WHITE, t1, 1);

    // Controls
    int btn_y = w->y + 5;
    int close_x = w->x + w->w - 25;
    
    // Close
    bool h_close = rect_contains(close_x, btn_y, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(close_x, btn_y, 20, 18, h_close ? COL_RED_HOVER : COL_RED, false);
    graphics_draw_char(close_x + 6, btn_y + 5, 'X', COL_WHITE, h_close ? COL_RED_HOVER : COL_RED);
    
    // Maximize
    int max_x = close_x - 22;
    bool h_max = rect_contains(max_x, btn_y, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(max_x, btn_y, 20, 18, h_max ? COL_BTN_HOVER : 0xFFDDDDDD, false);
    graphics_draw_char(max_x + 6, btn_y + 5, w->maximized ? '^' : 'O', COL_BLACK, h_max ? COL_BTN_HOVER : 0xFFDDDDDD);

    // Minimize
    int min_x = max_x - 22;
    bool h_min = rect_contains(min_x, btn_y, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(min_x, btn_y, 20, 18, h_min ? COL_BTN_HOVER : 0xFFDDDDDD, false);
    graphics_draw_char(min_x + 6, btn_y + 5, '_', COL_BLACK, h_min ? COL_BTN_HOVER : 0xFFDDDDDD);

    // Client Area
    int cx = w->x + 4;
    int cy = w->y + WIN_CAPTION_H + 4;
    if (w->type == APP_NOTEPAD) render_notepad(w, cx, cy);
    else if (w->type == APP_CALC) render_calc(w, cx, cy);
    else if (w->type == APP_FILES) render_file_manager(w, cx, cy);
    else if (w->type == APP_WELCOME) render_system_info(w, cx, cy);
}

static void render_desktop(void) {
    draw_gradient_rect(0, 0, screen_w, screen_h - TASKBAR_H, COL_DESKTOP_TOP, COL_DESKTOP_BOT);
    
    // Icons
    struct { int x, y; const char* lbl; } icons[] = {
        {20, 20, "My PC"}, {20, 80, "My Files"}, {20, 140, "Notepad"}, {20, 200, "Calc"}
    };
    for (int i=0; i<4; i++) {
        bool hover = rect_contains(icons[i].x, icons[i].y, 64, 55, mouse.x, mouse.y);
        if (hover) graphics_fill_rect(icons[i].x, icons[i].y, 64, 55, 0x40FFFFFF);
        uint32_t icol = (i == 1) ? 0xFFDDAA00 : 0xFFEEEEEE;
        graphics_fill_rect(icons[i].x+16, icons[i].y+5, 32, 28, icol);
        graphics_draw_string_scaled(icons[i].x+6, icons[i].y+41, icons[i].lbl, COL_BLACK, 0, 1);
        graphics_draw_string_scaled(icons[i].x+5, icons[i].y+40, icons[i].lbl, COL_WHITE, 0, 1);
    }

    // Windows (Bottom to Top)
    for (int i=0; i<MAX_WINDOWS; i++) {
        if (windows[i]) render_window(windows[i]);
    }

    // Taskbar
    int ty = screen_h - TASKBAR_H;
    draw_gradient_rect(0, ty, screen_w, TASKBAR_H, 0xFF245580, COL_TASKBAR);
    graphics_fill_rect(0, ty, screen_w, 1, 0xFF507090);
    
    bool h_start = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    draw_gradient_rect(0, ty, 80, TASKBAR_H, (h_start||start_menu_open)?COL_START_HOVER:COL_START_BTN, COL_TASKBAR);
    graphics_fill_rect(10, ty+8, 14, 14, 0xFF00FF00); 
    graphics_draw_string_scaled(30, ty+10, "Start", COL_WHITE, 0, 1);
    
    // Clock
    char time_buf[8]; get_time_string(time_buf);
    int clock_x = screen_w - 60;
    draw_bevel_rect(clock_x, ty+4, 56, TASKBAR_H-8, 0xFF153050, true);
    graphics_draw_string_scaled(clock_x+8, ty+10, time_buf, COL_WHITE, 0xFF153050, 1);

    // Tabs
    int tx = 90;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i] && windows[i]->visible) {
            bool active = windows[i]->focused && !windows[i]->minimized;
            draw_bevel_rect(tx, ty+4, 100, TASKBAR_H-8, active?0xFF3A6EA5:0xFF2A4E75, !active);
            char st[11]; int c=0; while(c<10 && windows[i]->title[c]){st[c]=windows[i]->title[c]; c++;} st[c]=0;
            graphics_draw_string_scaled(tx+5, ty+10, st, COL_WHITE, active?0xFF3A6EA5:0xFF2A4E75, 1);
            tx += 105;
        }
    }

    // Start Menu
    if (start_menu_open) {
        int mw = 160, mh = 240, my = ty - mh;
        draw_bevel_rect(0, my, mw, mh, COL_WIN_BODY, false);
        graphics_fill_rect(0, my, 24, mh, COL_START_BTN);
        struct { int y; const char* lbl; } items[] = {{my+10, "My PC"}, {my+40, "File Explorer"}, {my+80, "Notepad"}, {my+110, "Calculator"}, {my+200, "Shutdown"}};
        for(int i=0; i<5; i++) {
            if (i==4) graphics_fill_rect(25, items[i].y-10, mw-30, 1, 0xFFAAAAAA);
            bool h = rect_contains(25, items[i].y, mw-30, 24, mouse.x, mouse.y);
            if(h) graphics_fill_rect(25, items[i].y, mw-30, 24, COL_BTN_HOVER);
            graphics_draw_string_scaled(35, items[i].y+6, items[i].lbl, h?COL_WHITE:COL_BLACK, h?COL_BTN_HOVER:COL_WIN_BODY, 1);
        }
    }

    // Mouse
    int mx = mouse.x, my = mouse.y;
    graphics_fill_rect(mx, my, 1, 14, COL_WHITE);
    graphics_fill_rect(mx, my, 10, 1, COL_WHITE);
    graphics_fill_rect(mx+1, my+1, 1, 12, COL_BLACK);
    graphics_fill_rect(mx+1, my+1, 8, 1, COL_BLACK);
}

static void toggle_maximize(Window* w) {
    if (w->maximized) {
        w->x = w->restore_x; w->y = w->restore_y; w->w = w->restore_w; w->h = w->restore_h;
        w->maximized = false;
    } else {
        w->restore_x = w->x; w->restore_y = w->y; w->restore_w = w->w; w->restore_h = w->h;
        w->x = 0; w->y = 0; w->w = screen_w; w->h = screen_h - TASKBAR_H;
        w->maximized = true;
    }
}

// Helper to process calculator logic safely to avoid warnings
static void handle_calc_logic(Window* w, char c) {
    CalcState* s = &w->state.calc;
    if (c >= '0' && c <= '9') {
        int d = c - '0';
        if (s->new_entry) {
            s->current_val = d;
            s->new_entry = false;
        } else if (s->current_val < 100000000) {
            s->current_val = s->current_val * 10 + d;
        }
    } else if (c == 'C') {
        s->current_val = 0;
        s->accumulator = 0;
        s->op = 0;
        s->new_entry = true;
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        s->accumulator = s->current_val;
        s->op = c;
        s->new_entry = true;
    } else if (c == '=') {
        if (s->op == '+') {
            s->current_val = s->accumulator + s->current_val;
        } else if (s->op == '-') {
            s->current_val = s->accumulator - s->current_val;
        } else if (s->op == '*') {
            s->current_val = s->accumulator * s->current_val;
        } else if (s->op == '/') {
            if (s->current_val != 0) {
                s->current_val = s->accumulator / s->current_val;
            }
        }
        s->op = 0;
        s->new_entry = true;
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;

    // Start Menu Click
    if (start_menu_open) {
        int my = ty - 240;
        if (x < 160 && y > my) {
            if (rect_contains(0, my+10, 160, 24, x, y)) create_window(APP_WELCOME, "My PC", 300, 200);
            else if (rect_contains(0, my+40, 160, 24, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
            else if (rect_contains(0, my+80, 160, 24, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(0, my+110, 160, 24, x, y)) create_window(APP_CALC, "Calculator", 220, 300);
            else if (rect_contains(0, my+200, 160, 30, x, y)) outw(0x604, 0x2000); 
            start_menu_open = false; return;
        }
        start_menu_open = false;
    }

    // Taskbar Click
    if (y >= ty) {
        if (x < 80) { start_menu_open = !start_menu_open; return; }
        int tx = 90;
        for (int i=0; i<MAX_WINDOWS; i++) {
            if (windows[i] && windows[i]->visible) {
                if (rect_contains(tx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i]->focused && !windows[i]->minimized) windows[i]->minimized = true;
                    else focus_window(i);
                    return;
                }
                tx += 105;
            }
        }
        return;
    }

    // Window Click
    // Iterate backwards to find top-most first
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (windows[i] && windows[i]->visible && !windows[i]->minimized) {
            if (rect_contains(windows[i]->x, windows[i]->y, windows[i]->w, windows[i]->h, x, y)) {
                focus_window(i);
                Window* w = windows[MAX_WINDOWS - 1]; // After focus, it's at the top

                // Buttons
                int by = w->y + 5, cx = w->x + w->w - 25;
                if (rect_contains(cx, by, 20, 18, x, y)) { close_window(MAX_WINDOWS-1); return; }
                if (rect_contains(cx-22, by, 20, 18, x, y)) { toggle_maximize(w); return; }
                if (rect_contains(cx-44, by, 20, 18, x, y)) { w->minimized = true; return; }
                
                // Title Drag
                if (y < w->y + WIN_CAPTION_H && !w->maximized) {
                    w->dragging = true; w->drag_off_x = x - w->x; w->drag_off_y = y - w->y; return;
                }
                
                // Calculator
                if (w->type == APP_CALC) {
                    const char* btns = "789/456*123-C0=+";
                    int sx = w->x + 14, sy = w->y + WIN_CAPTION_H + 54;
                    for(int b=0; b<16; b++) {
                        if (rect_contains(sx + (b%4)*40, sy + (b/4)*35, 35, 30, x, y)) {
                            handle_calc_logic(w, btns[b]);
                            return;
                        }
                    }
                }
                
                // File Manager
                if (w->type == APP_FILES) {
                    int ly = w->y + WIN_CAPTION_H + 28; size_t cnt = fs_file_count();
                    for(size_t f=0; f<cnt; f++) {
                        if (rect_contains(w->x+4, ly + (f*18), w->w-8, 18, x, y)) { w->state.files.selected_index = f; return; }
                    }
                }
                return;
            }
        }
    }

    // Desktop Icons
    if (rect_contains(20, 20, 64, 55, x, y)) create_window(APP_WELCOME, "My PC", 300, 200);
    else if (rect_contains(20, 80, 64, 55, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
    else if (rect_contains(20, 140, 64, 55, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 200, 64, 55, x, y)) create_window(APP_CALC, "Calc", 220, 300);
}

void gui_demo_run(void) {
    // CRITICAL: Enable interrupts to allow Timer and Mouse events to fire!
    // Without this, timer_wait() will hang forever.
    __asm__ volatile("sti");

    syslog_write("GUI: Starting desktop environment...");
    
    graphics_enable_double_buffer();
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    mouse_init();

    for(int i=0; i<MAX_WINDOWS; i++) windows[i] = NULL;
    start_menu_open = false;
    create_window(APP_WELCOME, "Welcome", 300, 160);

    bool running = true;
    while(running) {
        // Remove explicit timer_wait. 
        // The heavy rendering loop acts as a natural frame limiter.
        // timer_wait(1); 

        char c = keyboard_poll_char();
        if (c == 27) running = false; // ESC to exit

        // Check if we have windows before accessing them
        Window* top = NULL;
        if (windows[MAX_WINDOWS-1] != NULL) {
            top = windows[MAX_WINDOWS-1];
        }

        if (c && top && top->visible && !top->minimized && top->type == APP_NOTEPAD) {
            NotepadState* ns = &top->state.notepad;
            if (c == '\b') { if (ns->length > 0) ns->buffer[--ns->length] = 0; }
            else if (c >= 32 && c <= 126 && ns->length < 250) { ns->buffer[ns->length++] = c; ns->buffer[ns->length] = 0; }
        }

        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        if (mouse.left_button && top && top->visible && top->dragging) {
            top->x = mouse.x - top->drag_off_x;
            top->y = mouse.y - top->drag_off_y;
        }
        
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        
        if (!mouse.left_button && prev_mouse.left_button) {
            for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) windows[i]->dragging = false;
        }

        render_desktop();
        graphics_swap_buffer();
    }

    // Cleanup
    for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) kfree(windows[i]);
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
}
