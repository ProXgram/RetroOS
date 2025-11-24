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
static Window windows[MAX_WINDOWS];
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
    // Wait for RTC update to complete (bit 7 of reg A)
    while (get_rtc_register(0x0A) & 0x80);
    
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

// Moves window at 'index' to the end of the array (top of Z-order)
static void focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return;
    
    if (index == MAX_WINDOWS - 1) {
        for(int i=0; i<MAX_WINDOWS-1; i++) windows[i].focused = false;
        windows[MAX_WINDOWS-1].focused = true;
        windows[MAX_WINDOWS-1].minimized = false;
        return;
    }

    Window temp = windows[index];
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[MAX_WINDOWS - 1] = temp;
    
    for(int i=0; i<MAX_WINDOWS; i++) windows[i].focused = false;
    windows[MAX_WINDOWS - 1].focused = true;
    windows[MAX_WINDOWS - 1].minimized = false;
}

static Window* create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) { slot = i; break; }
    }
    if (slot == -1) slot = 0; 

    Window* win = &windows[slot];
    win->id = slot;
    win->type = type;
    str_copy(win->title, title);
    win->w = w; win->h = h;
    
    static int cascade_offset = 0;
    win->x = 40 + cascade_offset;
    win->y = 40 + cascade_offset;
    cascade_offset = (cascade_offset + 25) % 150;
    
    if (win->x + w > screen_w) win->x = 20;
    if (win->y + h > screen_h - TASKBAR_H) win->y = 20;

    win->visible = true;
    win->minimized = false;
    win->maximized = false;
    win->dragging = false;
    
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

    focus_window(slot);
    return &windows[MAX_WINDOWS - 1];
}

// --- Drawing ---

static void draw_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i=0; i<h; i++) {
        // Simple linear interpolation
        uint8_t r1 = (c1 >> 16) & 0xFF;
        uint8_t g1 = (c1 >> 8) & 0xFF;
        uint8_t b1 = c1 & 0xFF;
        
        uint8_t r2 = (c2 >> 16) & 0xFF;
        uint8_t g2 = (c2 >> 8) & 0xFF;
        uint8_t b2 = c2 & 0xFF;
        
        uint8_t r = r1 + ((r2 - r1) * i) / h;
        uint8_t g = g1 + ((g2 - g1) * i) / h;
        uint8_t b = b1 + ((b2 - b1) * i) / h;
        
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

    // Header
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
        
        if (row_y + row_h > cy + ch) break; // Clip

        bool selected = ((int)i == w->state.files.selected_index);
        if (selected) {
            graphics_fill_rect(cx+2, row_y, cw-4, row_h, 0xFF316AC5);
        }

        uint32_t txt_col = selected ? COL_WHITE : COL_BLACK;
        uint32_t bg_col = selected ? 0xFF316AC5 : COL_WHITE;

        // Icon placeholder
        graphics_fill_rect(cx+4, row_y+2, 12, 12, 0xFFFFCC00);

        // Filename
        graphics_draw_string_scaled(cx+20, row_y+4, f->name, txt_col, bg_col, 1);

        // Size
        char size_buf[16];
        int_to_str(f->size, size_buf);
        graphics_draw_string_scaled(cx+cw-80, row_y+4, size_buf, txt_col, bg_col, 1);
        graphics_draw_string_scaled(cx+cw-40, row_y+4, "B", txt_col, bg_col, 1);

        y_off += row_h;
    }
}

static void render_system_info(Window* w, int cx, int cy) {
    (void)w;
    graphics_draw_string_scaled(cx+20, cy+20, "NostaluxOS v1.0", COL_BLACK, COL_WIN_BODY, 2);
    
    const struct system_profile* p = system_profile_info();
    char mem[32];
    int_to_str(p->memory_total_kb / 1024, mem);
    
    char res[32];
    int_to_str(screen_w, res);
    
    graphics_draw_string_scaled(cx+20, cy+60, "Memory: ", 0xFF555555, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+90, cy+60, mem, COL_BLACK, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+120, cy+60, "MB", COL_BLACK, COL_WIN_BODY, 1);

    graphics_draw_string_scaled(cx+20, cy+80, "Display:", 0xFF555555, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+90, cy+80, res, COL_BLACK, COL_WIN_BODY, 1);
    graphics_draw_string_scaled(cx+130, cy+80, "px", COL_BLACK, COL_WIN_BODY, 1);
}

static void render_window(Window* w) {
    if (!w->visible || w->minimized) return;

    // 1. Shadow
    if (!w->maximized) {
        graphics_fill_rect(w->x + 4, w->y + 4, w->w, w->h, 0x50000000);
    }

    // 2. Main Body
    graphics_fill_rect(w->x, w->y, w->w, w->h, COL_WIN_BODY);
    
    // 3. Title Bar Gradient
    uint32_t t1 = w->focused ? COL_WIN_TITLE_1 : COL_WIN_INACT_1;
    uint32_t t2 = w->focused ? COL_WIN_TITLE_2 : COL_WIN_INACT_2;
    draw_gradient_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H, t1, t2);
    
    // Title Text
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_WHITE, t1, 1);

    // 4. Controls
    // Close Button
    int btn_y = w->y + 5;
    int close_x = w->x + w->w - 25;
    bool hover_close = rect_contains(close_x, btn_y, 20, 20, mouse.x, mouse.y);
    draw_bevel_rect(close_x, btn_y, 20, 18, hover_close ? COL_RED_HOVER : COL_RED, false);
    graphics_draw_char(close_x + 6, btn_y + 5, 'X', COL_WHITE, hover_close ? COL_RED_HOVER : COL_RED);
    
    // Maximize/Restore Button
    int max_x = close_x - 22;
    bool hover_max = rect_contains(max_x, btn_y, 20, 20, mouse.x, mouse.y);
    draw_bevel_rect(max_x, btn_y, 20, 18, hover_max ? COL_BTN_HOVER : 0xFFDDDDDD, false);
    graphics_draw_char(max_x + 6, btn_y + 5, w->maximized ? '^' : 'O', COL_BLACK, hover_max ? COL_BTN_HOVER : 0xFFDDDDDD);

    // Minimize Button
    int min_x = max_x - 22;
    bool hover_min = rect_contains(min_x, btn_y, 20, 20, mouse.x, mouse.y);
    draw_bevel_rect(min_x, btn_y, 20, 18, hover_min ? COL_BTN_HOVER : 0xFFDDDDDD, false);
    graphics_draw_char(min_x + 6, btn_y + 5, '_', COL_BLACK, hover_min ? COL_BTN_HOVER : 0xFFDDDDDD);

    // 5. Client Area
    int cx = w->x + 4;
    int cy = w->y + WIN_CAPTION_H + 4;
    
    if (w->type == APP_NOTEPAD) render_notepad(w, cx, cy);
    else if (w->type == APP_CALC) render_calc(w, cx, cy);
    else if (w->type == APP_FILES) render_file_manager(w, cx, cy);
    else if (w->type == APP_WELCOME) render_system_info(w, cx, cy);
}

static void render_desktop(void) {
    // 1. Wallpaper Gradient
    draw_gradient_rect(0, 0, screen_w, screen_h - TASKBAR_H, COL_DESKTOP_TOP, COL_DESKTOP_BOT);
    
    // 2. Desktop Icons
    struct { int x, y; const char* lbl; } icons[] = {
        {20, 20, "My PC"},
        {20, 80, "My Files"},
        {20, 140, "Notepad"},
        {20, 200, "Calc"},
    };
    
    for (int i=0; i<4; i++) {
        bool hover = rect_contains(icons[i].x, icons[i].y, 64, 55, mouse.x, mouse.y);
        if (hover) graphics_fill_rect(icons[i].x, icons[i].y, 64, 55, 0x40FFFFFF);
        
        // Icon Body
        uint32_t icol = (i == 1) ? 0xFFDDAA00 : 0xFFEEEEEE; // Yellow for folders
        graphics_fill_rect(icons[i].x+16, icons[i].y+5, 32, 28, icol);
        
        // Label with shadow
        graphics_draw_string_scaled(icons[i].x+6, icons[i].y+41, icons[i].lbl, COL_BLACK, 0, 1);
        graphics_draw_string_scaled(icons[i].x+5, icons[i].y+40, icons[i].lbl, COL_WHITE, 0, 1);
    }

    // 3. Windows
    for (int i=0; i<MAX_WINDOWS; i++) render_window(&windows[i]);

    // 4. Taskbar
    int ty = screen_h - TASKBAR_H;
    draw_gradient_rect(0, ty, screen_w, TASKBAR_H, 0xFF245580, COL_TASKBAR);
    graphics_fill_rect(0, ty, screen_w, 1, 0xFF507090);
    
    // Start Button
    bool hover_start = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    uint32_t start_c1 = (hover_start || start_menu_open) ? COL_START_HOVER : COL_START_BTN;
    uint32_t start_c2 = COL_TASKBAR;
    draw_gradient_rect(0, ty, 80, TASKBAR_H, start_c1, start_c2);
    // Start Logo
    graphics_fill_rect(10, ty+8, 14, 14, 0xFF00FF00); 
    graphics_draw_string_scaled(30, ty+10, "Start", COL_WHITE, 0, 1);
    
    // Clock (Right side)
    char time_buf[8];
    get_time_string(time_buf);
    int clock_w = 60;
    int clock_x = screen_w - clock_w;
    draw_bevel_rect(clock_x, ty+4, clock_w-4, TASKBAR_H-8, 0xFF153050, true);
    graphics_draw_string_scaled(clock_x+8, ty+10, time_buf, COL_WHITE, 0xFF153050, 1);

    // Window Tabs
    int tx = 90;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i].visible) {
            bool active = windows[i].focused && !windows[i].minimized;
            uint32_t tcol = active ? 0xFF3A6EA5 : 0xFF2A4E75;
            draw_bevel_rect(tx, ty+4, 100, TASKBAR_H-8, tcol, !active);
            
            char short_title[11];
            int c=0; while(c<10 && windows[i].title[c]){short_title[c]=windows[i].title[c]; c++;} short_title[c]=0;
            graphics_draw_string_scaled(tx+5, ty+10, short_title, COL_WHITE, tcol, 1);
            tx += 105;
        }
    }

    // 5. Start Menu
    if (start_menu_open) {
        int mw = 160;
        int mh = 240;
        int my = ty - mh;
        draw_bevel_rect(0, my, mw, mh, COL_WIN_BODY, false);
        
        // Sidebar
        graphics_fill_rect(0, my, 24, mh, COL_START_BTN);
        
        struct { int y; const char* lbl; } items[] = {
            {my+10, "My PC"},
            {my+40, "File Explorer"},
            {my+80, "Notepad"},
            {my+110, "Calculator"},
            {my+200, "Shutdown"}
        };
        
        for(int i=0; i<5; i++) {
            if (i==4) graphics_fill_rect(25, items[i].y-10, mw-30, 1, 0xFFAAAAAA); // Separator
            
            bool h = rect_contains(25, items[i].y, mw-30, 24, mouse.x, mouse.y);
            uint32_t bg = h ? COL_BTN_HOVER : COL_WIN_BODY;
            uint32_t fg = h ? COL_WHITE : COL_BLACK;
            if(h) graphics_fill_rect(25, items[i].y, mw-30, 24, bg);
            graphics_draw_string_scaled(35, items[i].y+6, items[i].lbl, fg, bg, 1);
        }
    }

    // 6. Mouse
    int mx = mouse.x; int my_pos = mouse.y;
    graphics_fill_rect(mx, my_pos, 1, 14, COL_WHITE);
    graphics_fill_rect(mx, my_pos, 10, 1, COL_WHITE);
    graphics_fill_rect(mx+1, my_pos+1, 1, 12, COL_BLACK); // Outline
    graphics_fill_rect(mx+1, my_pos+1, 8, 1, COL_BLACK);
}

// --- Logic ---

static void handle_calc_logic(Window* w, char c) {
    CalcState* s = &w->state.calc;
    if (c >= '0' && c <= '9') {
        int d = c - '0';
        if (s->new_entry) { s->current_val = d; s->new_entry = false; }
        else if (s->current_val < 100000000) s->current_val = s->current_val * 10 + d;
    } else if (c == 'C') {
        s->current_val = 0; s->accumulator = 0; s->op = 0; s->new_entry = true;
    } else if (c=='+'||c=='-'||c=='*'||c=='/') {
        s->accumulator = s->current_val; s->op = c; s->new_entry = true;
    } else if (c == '=') {
        if (s->op == '+') s->current_val = s->accumulator + s->current_val;
        else if (s->op == '-') s->current_val = s->accumulator - s->current_val;
        else if (s->op == '*') s->current_val = s->accumulator * s->current_val;
        else if (s->op == '/' && s->current_val != 0) s->current_val = s->accumulator / s->current_val;
        s->op = 0; s->new_entry = true;
    }
}

static void toggle_maximize(Window* w) {
    if (w->maximized) {
        // Restore
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->w = w->restore_w;
        w->h = w->restore_h;
        w->maximized = false;
    } else {
        // Maximize
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->w;
        w->restore_h = w->h;
        
        w->x = 0;
        w->y = 0;
        w->w = screen_w;
        w->h = screen_h - TASKBAR_H;
        w->maximized = true;
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;

    if (start_menu_open) {
        int mw = 160; int mh = 240; int my = ty - mh;
        if (rect_contains(0, my, mw, mh, x, y)) {
            if (rect_contains(0, my+10, mw, 24, x, y)) create_window(APP_WELCOME, "My PC", 300, 200);
            else if (rect_contains(0, my+40, mw, 24, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
            else if (rect_contains(0, my+80, mw, 24, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(0, my+110, mw, 24, x, y)) create_window(APP_CALC, "Calculator", 220, 300);
            else if (rect_contains(0, my+200, mw, 30, x, y)) outw(0x604, 0x2000); 
            start_menu_open = false;
            return;
        }
        start_menu_open = false;
    }

    if (y >= ty) {
        if (x < 80) { start_menu_open = !start_menu_open; return; }
        
        int tx = 90;
        for (int i=0; i<MAX_WINDOWS; i++) {
            if (windows[i].visible) {
                if (rect_contains(tx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i].focused && !windows[i].minimized) windows[i].minimized = true;
                    else focus_window(i);
                    return;
                }
                tx += 105;
            }
        }
        return;
    }

    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = &windows[i];
        if (!w->visible || w->minimized) continue;
        
        if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
            focus_window(i);
            w = &windows[MAX_WINDOWS - 1]; 
            
            // Title Bar Buttons
            int btn_y = w->y + 5;
            int close_x = w->x + w->w - 25;
            if (rect_contains(close_x, btn_y, 20, 18, x, y)) { w->visible = false; return; }
            
            int max_x = close_x - 22;
            if (rect_contains(max_x, btn_y, 20, 18, x, y)) { toggle_maximize(w); return; }

            int min_x = max_x - 22;
            if (rect_contains(min_x, btn_y, 20, 18, x, y)) { w->minimized = true; return; }
            
            // Drag Start
            if (y < w->y + WIN_CAPTION_H && !w->maximized) {
                w->dragging = true;
                w->drag_off_x = x - w->x;
                w->drag_off_y = y - w->y;
                return;
            }
            
            // App Interactions
            if (w->type == APP_CALC) {
                const char* btns = "789/456*123-C0=+";
                int start_x = w->x + 14; int start_y = w->y + WIN_CAPTION_H + 54;
                for(int b=0; b<16; b++) {
                    int r = b/4; int c = b%4;
                    if (rect_contains(start_x + c*40, start_y + r*35, 35, 30, x, y)) {
                        handle_calc_logic(w, btns[b]); return;
                    }
                }
            } else if (w->type == APP_FILES) {
                int list_y = w->y + WIN_CAPTION_H + 4 + 24;
                int count = fs_file_count();
                for(int f=0; f<count; f++) {
                    if (rect_contains(w->x+4, list_y + (f*18), w->w-8, 18, x, y)) {
                        w->state.files.selected_index = f;
                        return;
                    }
                }
            }
            return; 
        }
    }

    // Desktop Icons
    if (rect_contains(20, 20, 64, 55, x, y)) create_window(APP_WELCOME, "My PC", 300, 200);
    else if (rect_contains(20, 80, 64, 55, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
    else if (rect_contains(20, 140, 64, 55, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 200, 64, 55, x, y)) create_window(APP_CALC, "Calc", 220, 300);
}

void gui_demo_run(void) {
    syslog_write("GUI: Starting enhanced desktop...");
    
    graphics_enable_double_buffer();
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    mouse_init();

    for(int i=0; i<MAX_WINDOWS; i++) windows[i].visible = false;
    start_menu_open = false;
    create_window(APP_WELCOME, "Welcome", 300, 160);

    bool running = true;
    while(running) {
        char c = keyboard_poll_char();
        if (c == 27) running = false;
        
        Window* top = &windows[MAX_WINDOWS-1];
        if (c && top->visible && !top->minimized && top->type == APP_NOTEPAD) {
            NotepadState* ns = &top->state.notepad;
            if (c == '\b') { if (ns->length > 0) ns->buffer[--ns->length] = 0; }
            else if (c >= 32 && c <= 126) {
                if (ns->length < 250) { ns->buffer[ns->length++] = c; ns->buffer[ns->length] = 0; }
            }
        }

        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        if (mouse.left_button && top->visible && top->dragging) {
            top->x = mouse.x - top->drag_off_x;
            top->y = mouse.y - top->drag_off_y;
        }
        
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        
        if (!mouse.left_button && prev_mouse.left_button) {
            for(int i=0; i<MAX_WINDOWS; i++) windows[i].dragging = false;
        }

        render_desktop();
        graphics_swap_buffer();
    }
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
}

//te
