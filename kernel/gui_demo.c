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
#include "heap.h"
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 16
#define WIN_CAPTION_H 28
#define TASKBAR_H 34

// --- Colors ---
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
#define COL_GREEN       0xFF00FF00

// --- Global Desktop Appearance ---
static uint32_t desktop_col_top = 0xFF2D73A8; // Default Blue
static uint32_t desktop_col_bot = 0xFF103050;

// --- Types ---
typedef enum { APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC, APP_FILES, APP_SETTINGS, APP_TERMINAL } AppType;

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
    int dummy; 
} SettingsState;

typedef struct {
    char prompt[16];
    char input[64];
    int input_len;
    char history[5][64]; // Last 5 lines of output
} TerminalState;

typedef struct {
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool maximized;
    bool focused;
    bool dragging;
    int drag_off_x, drag_off_y;
    int restore_x, restore_y, restore_w, restore_h;

    union { 
        CalcState calc; 
        NotepadState notepad; 
        FileManagerState files;
        SettingsState settings;
        TerminalState term;
    } state;
} Window;

static Window* windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;

// --- Forward Declarations ---
static void render_system_info(Window* w, int cx, int cy);
static void handle_calc_logic(Window* w, char key);
static void handle_settings_click(Window* w, int x, int y);
static void handle_terminal_input(Window* w, char c);
static void render_terminal(Window* w, int cx, int cy);
static void toggle_maximize(Window* w);
static void close_window(int index);
static int focus_window(int index);
static void create_window(AppType type, const char* title, int w, int h);

// --- Cursor Bitmap (Arrow) ---
static const uint8_t CURSOR_BITMAP[19][12] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,1},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,1,1,2,2,1,0,0,0,0,0},
    {1,1,0,1,2,2,1,0,0,0,0,0},
    {0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0}
};

// --- RTC ---
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71
static uint8_t get_rtc_register(int reg) { outb(CMOS_ADDRESS, (uint8_t)reg); return inb(CMOS_DATA); }
static void get_time_string(char* buf) {
    int timeout = 1000; while ((get_rtc_register(0x0A) & 0x80) && timeout-- > 0);
    uint8_t min = get_rtc_register(0x02); uint8_t hour = get_rtc_register(0x04);
    min = (min & 0x0F) + ((min / 16) * 10); hour = ((hour & 0x0F) + ((hour / 16) * 10));
    buf[0] = '0' + (hour / 10); buf[1] = '0' + (hour % 10); buf[2] = ':';
    buf[3] = '0' + (min / 10); buf[4] = '0' + (min % 10); buf[5] = 0;
}

// --- Helpers ---
static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}
static void str_copy(char* dest, const char* src) {
    int i = 0; while (src[i] && i < 63) { dest[i] = src[i]; i++; } dest[i] = 0;
}
static void int_to_str(int v, char* buf) {
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    bool neg = v < 0; if (neg) v = -v;
    int i = 0; char tmp[16]; while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0; while (i > 0) buf[j++] = tmp[--i]; buf[j] = 0;
}

// --- Window Management ---
static Window* get_top_window(void) {
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) { if (windows[i] != NULL) return windows[i]; } return NULL;
}
static int focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return -1;
    Window* target = windows[index];
    int i = index;
    while (i < MAX_WINDOWS - 1 && windows[i+1] != NULL) { windows[i] = windows[i+1]; i++; }
    windows[i] = target;
    for(int j=0; j<MAX_WINDOWS; j++) if(windows[j]) {
        windows[j]->focused = (windows[j] == target);
        if (windows[j]->focused) windows[j]->minimized = false;
    }
    return i;
}
static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    kfree(windows[index]); windows[index] = NULL;
    for (int i = index; i < MAX_WINDOWS - 1; i++) windows[i] = windows[i+1];
    windows[MAX_WINDOWS - 1] = NULL;
}
static void create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) { if (windows[i] == NULL) { slot = i; break; } }
    if (slot == -1) { close_window(0); slot = MAX_WINDOWS - 1; }
    Window* win = (Window*)kmalloc(sizeof(Window));
    if (!win) { syslog_write("GUI: OOM"); return; }
    win->id = slot; win->type = type; str_copy(win->title, title);
    win->w = w; win->h = h; win->x = 40 + (slot*20)%160; win->y = 40 + (slot*20)%160;
    
    if (win->x + w > screen_w) win->x = 20; 
    if (win->y + h > screen_h - TASKBAR_H) win->y = 20;
    
    win->visible = true; win->minimized = false; win->maximized = false; win->dragging = false; win->focused = true;
    if (type == APP_NOTEPAD) { win->state.notepad.length = 0; win->state.notepad.buffer[0] = 0; }
    else if (type == APP_CALC) { win->state.calc.current_val = 0; win->state.calc.new_entry = true; }
    else if (type == APP_FILES) { win->state.files.selected_index = -1; win->state.files.scroll_offset = 0; }
    else if (type == APP_TERMINAL) {
        str_copy(win->state.term.prompt, "> ");
        win->state.term.input[0] = 0;
        win->state.term.input_len = 0;
        for(int k=0; k<5; k++) win->state.term.history[k][0] = 0;
        str_copy(win->state.term.history[0], "Nostalux Shell v1.0");
        str_copy(win->state.term.history[1], "Type 'help' for cmds");
    }
    
    windows[slot] = win; focus_window(slot);
}

// --- Drawing ---
static void draw_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i=0; i<h; i++) {
        uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
        uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
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
    graphics_fill_rect(x, y, w, 1, tl); graphics_fill_rect(x, y, 1, h, tl);      
    graphics_fill_rect(x, y+h-1, w, 1, br); graphics_fill_rect(x+w-1, y, 1, h, br);  
}

// --- App Renderers ---
static void render_notepad(Window* w, int cx, int cy) {
    draw_bevel_rect(cx+2, cy+2, w->w-4, w->h-WIN_CAPTION_H-4, COL_WHITE, true);
    graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
    if ((timer_get_ticks() / 15) % 2) graphics_fill_rect(cx+6+(w->state.notepad.length*8), cy+6, 2, 10, COL_BLACK);
}
static void render_calc(Window* w, int cx, int cy) {
    char buf[16]; int_to_str(w->state.calc.current_val, buf);
    draw_bevel_rect(cx+10, cy+10, w->w-20, 30, COL_WHITE, true);
    graphics_draw_string_scaled(cx+w->w-20-(kstrlen(buf)*16), cy+15, buf, COL_BLACK, COL_WHITE, 2);
    const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    for(int i=0; i<16; i++) {
        int bx = cx+10 + (i%4)*40, by = cy+50 + (i/4)*35;
        bool h = rect_contains(bx, by, 35, 30, mouse.x, mouse.y);
        bool p = h && mouse.left_button;
        draw_bevel_rect(bx, by, 35, 30, p?COL_BTN_PRESS:(h?COL_BTN_HOVER:0xFFDDDDDD), p);
        graphics_draw_char(bx+12+(p?1:0), by+10+(p?1:0), btns[i][0], COL_BLACK, 0);
    }
}
static void render_file_manager(Window* w, int cx, int cy) {
    draw_bevel_rect(cx, cy, w->w-8, w->h-WIN_CAPTION_H-8, COL_WHITE, true);
    graphics_fill_rect(cx+1, cy+1, w->w-10, 20, 0xFFEEEEEE);
    graphics_draw_string_scaled(cx+5, cy+7, "Name", COL_BLACK, 0xFFEEEEEE, 1);
    size_t count = fs_file_count();
    for (size_t i = 0; i < count; i++) {
        const struct fs_file* f = fs_file_at(i); if(!f) continue;
        int ry = cy + 24 + i*18; if(ry > cy+w->h-40) break;
        bool s = ((int)i == w->state.files.selected_index);
        if (s) graphics_fill_rect(cx+2, ry, w->w-12, 18, 0xFF316AC5);
        graphics_draw_string_scaled(cx+20, ry+4, f->name, s?COL_WHITE:COL_BLACK, s?0xFF316AC5:COL_WHITE, 1);
        char sb[16]; int_to_str(f->size, sb);
        graphics_draw_string_scaled(cx+w->w-60, ry+4, sb, s?COL_WHITE:COL_BLACK, s?0xFF316AC5:COL_WHITE, 1);
    }
}
static void render_terminal(Window* w, int cx, int cy) {
    // Black Background
    graphics_fill_rect(cx, cy, w->w-8, w->h-WIN_CAPTION_H-8, COL_BLACK);
    
    // Draw history
    for(int i=0; i<5; i++) {
        graphics_draw_string_scaled(cx+4, cy+4+(i*10), w->state.term.history[i], COL_GREEN, COL_BLACK, 1);
    }
    
    // Draw Prompt + Input
    int input_y = cy + 4 + (5*10);
    graphics_draw_string_scaled(cx+4, input_y, w->state.term.prompt, COL_GREEN, COL_BLACK, 1);
    graphics_draw_string_scaled(cx+20, input_y, w->state.term.input, COL_WHITE, COL_BLACK, 1);
    
    // Cursor
    if ((timer_get_ticks() / 15) % 2) {
        int cursor_x = cx + 20 + (w->state.term.input_len * 8);
        graphics_fill_rect(cursor_x, input_y, 8, 8, COL_GREEN);
    }
}

static void render_settings(Window* w, int cx, int cy) {
    draw_bevel_rect(cx, cy, w->w-8, w->h-WIN_CAPTION_H-8, COL_WIN_BODY, false);
    
    // Background Color Section
    graphics_draw_string_scaled(cx+10, cy+10, "Background Color:", COL_BLACK, COL_WIN_BODY, 1);
    
    uint32_t colors[] = { 0xFF2D73A8, 0xFF2D882D, 0xFF882D2D, 0xFF333333 }; // Blue, Green, Red, Black
    for (int i = 0; i < 4; i++) {
        int bx = cx + 10 + (i * 40);
        int by = cy + 30;
        
        bool h = rect_contains(bx, by, 30, 30, mouse.x, mouse.y);
        if (h) graphics_fill_rect(bx-2, by-2, 34, 34, COL_BLACK); // Highlight
        graphics_fill_rect(bx, by, 30, 30, colors[i]);
    }

    // Mouse Sensitivity Section
    graphics_draw_string_scaled(cx+10, cy+80, "Mouse Speed:", COL_BLACK, COL_WIN_BODY, 1);
    const char* speeds[] = { "Slow", "Med", "Fast" };
    int sense_map[] = { 1, 2, 4 };
    int current = mouse_get_sensitivity();
    
    for (int i = 0; i < 3; i++) {
        int bx = cx + 10 + (i * 60);
        int by = cy + 100;
        bool active = (current == sense_map[i]);
        bool h = rect_contains(bx, by, 50, 24, mouse.x, mouse.y);
        bool p = h && mouse.left_button;
        
        uint32_t col = active ? COL_START_HOVER : (h ? COL_BTN_HOVER : 0xFFDDDDDD);
        draw_bevel_rect(bx, by, 50, 24, col, active || p);
        
        uint32_t txt = active ? COL_WHITE : COL_BLACK;
        graphics_draw_string_scaled(bx+10, by+8, speeds[i], txt, col, 1);
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
    if (!w->maximized) graphics_fill_rect(w->x+4, w->y+4, w->w, w->h, 0x50000000);
    graphics_fill_rect(w->x, w->y, w->w, w->h, COL_WIN_BODY);
    
    uint32_t t1 = w->focused ? COL_WIN_TITLE_1 : COL_WIN_INACT_1;
    uint32_t t2 = w->focused ? COL_WIN_TITLE_2 : COL_WIN_INACT_2;
    draw_gradient_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H, t1, t2);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_WHITE, t1, 1);

    int bx = w->x+w->w-25;
    bool hc = rect_contains(bx, w->y+5, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(bx, w->y+5, 20, 18, hc?COL_RED_HOVER:COL_RED, false);
    graphics_draw_char(bx+6, w->y+10, 'X', COL_WHITE, hc?COL_RED_HOVER:COL_RED);
    
    int mx = bx-22;
    bool hm = rect_contains(mx, w->y+5, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(mx, w->y+5, 20, 18, hm?COL_BTN_HOVER:0xFFDDDDDD, false);
    graphics_draw_char(mx+6, w->y+10, w->maximized?'^':'O', COL_BLACK, 0);

    int mn = mx-22;
    bool hmn = rect_contains(mn, w->y+5, 20, 18, mouse.x, mouse.y);
    draw_bevel_rect(mn, w->y+5, 20, 18, hmn?COL_BTN_HOVER:0xFFDDDDDD, false);
    graphics_draw_char(mn+6, w->y+10, '_', COL_BLACK, 0);

    int cx = w->x+4, cy = w->y+WIN_CAPTION_H+4;
    if (w->type == APP_NOTEPAD) render_notepad(w, cx, cy);
    else if (w->type == APP_CALC) render_calc(w, cx, cy);
    else if (w->type == APP_FILES) render_file_manager(w, cx, cy);
    else if (w->type == APP_SETTINGS) render_settings(w, cx, cy);
    else if (w->type == APP_TERMINAL) render_terminal(w, cx, cy);
    else render_system_info(w, cx, cy);
}

static void render_cursor(void) {
    int mx = mouse.x;
    int my = mouse.y;
    
    for(int y=0; y<19; y++) {
        for(int x=0; x<12; x++) {
            uint8_t p = CURSOR_BITMAP[y][x];
            if(p == 1) graphics_put_pixel(mx+x, my+y, COL_BLACK);
            else if(p == 2) graphics_put_pixel(mx+x, my+y, COL_WHITE);
        }
    }
}

static void render_desktop(void) {
    draw_gradient_rect(0, 0, screen_w, screen_h - TASKBAR_H, desktop_col_top, desktop_col_bot);
    
    // "Click to capture" hint
    graphics_draw_string_scaled(screen_w - 220, 10, "Click screen to capture mouse", 0x80FFFFFF, 0, 1);

    struct { int x, y; const char* lbl; } icons[] = {
        {20, 20, "Terminal"}, {20, 80, "My Files"}, {20, 140, "Notepad"}, {20, 200, "Calc"}
    };
    for (int i=0; i<4; i++) {
        bool h = rect_contains(icons[i].x, icons[i].y, 64, 55, mouse.x, mouse.y);
        if (h) graphics_fill_rect(icons[i].x, icons[i].y, 64, 55, 0x40FFFFFF);
        // If it's terminal, use black screen icon
        uint32_t icol = (i==0) ? COL_BLACK : 0xFFEEEEEE;
        graphics_fill_rect(icons[i].x+16, icons[i].y+5, 32, 28, icol);
        if (i==0) graphics_draw_string_scaled(icons[i].x+18, icons[i].y+7, ">_", COL_GREEN, COL_BLACK, 1);
        
        graphics_draw_string_scaled(icons[i].x+5, icons[i].y+40, icons[i].lbl, COL_WHITE, 0, 1);
    }

    for (int i=0; i<MAX_WINDOWS; i++) if(windows[i]) render_window(windows[i]);

    int ty = screen_h - TASKBAR_H;
    draw_gradient_rect(0, ty, screen_w, TASKBAR_H, 0xFF245580, COL_TASKBAR);
    bool hs = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    draw_gradient_rect(0, ty, 80, TASKBAR_H, (hs||start_menu_open)?COL_START_HOVER:COL_START_BTN, COL_TASKBAR);
    graphics_draw_string_scaled(30, ty+10, "Start", COL_WHITE, 0, 1);

    char time_buf[8]; get_time_string(time_buf);
    draw_bevel_rect(screen_w-60, ty+4, 56, TASKBAR_H-8, 0xFF153050, true);
    graphics_draw_string_scaled(screen_w-52, ty+10, time_buf, COL_WHITE, 0xFF153050, 1);

    int tx = 90;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i] && windows[i]->visible) {
            bool a = windows[i]->focused && !windows[i]->minimized;
            draw_bevel_rect(tx, ty+4, 100, TASKBAR_H-8, a?0xFF3A6EA5:0xFF2A4E75, !a);
            char s[11]; int c=0; while(c<10&&windows[i]->title[c]){s[c]=windows[i]->title[c];c++;}s[c]=0;
            graphics_draw_string_scaled(tx+5, ty+10, s, COL_WHITE, 0, 1);
            tx += 105;
        }
    }

    if (start_menu_open) {
        int mh = 280, my = ty - mh;
        draw_bevel_rect(0, my, 160, mh, COL_WIN_BODY, false);
        graphics_fill_rect(0, my, 24, mh, COL_START_BTN);
        struct { int y; const char* lbl; } items[] = {
            {my+10, "Terminal"}, {my+40, "File Explorer"}, {my+80, "Notepad"}, 
            {my+110, "Calculator"}, {my+150, "Settings"}, {my+240, "Shutdown"}
        };
        for(int i=0; i<6; i++) {
            if(i==5) graphics_fill_rect(25, items[i].y-10, 130, 1, 0xFFAAAAAA);
            bool h = rect_contains(25, items[i].y, 130, 24, mouse.x, mouse.y);
            if(h) graphics_fill_rect(25, items[i].y, 130, 24, COL_BTN_HOVER);
            graphics_draw_string_scaled(35, items[i].y+6, items[i].lbl, h?COL_WHITE:COL_BLACK, h?COL_BTN_HOVER:COL_WIN_BODY, 1);
        }
    }

    render_cursor();
}

static void handle_settings_click(Window* w, int x, int y) {
    int cx = w->x + 4; 
    int cy = w->y + WIN_CAPTION_H + 4;
    
    // Colors
    uint32_t colors_top[] = { 0xFF2D73A8, 0xFF2D882D, 0xFF882D2D, 0xFF333333 };
    uint32_t colors_bot[] = { 0xFF103050, 0xFF103010, 0xFF301010, 0xFF000000 };
    
    for (int i = 0; i < 4; i++) {
        int bx = cx + 10 + (i * 40);
        int by = cy + 30;
        if (rect_contains(bx, by, 30, 30, x, y)) {
            desktop_col_top = colors_top[i];
            desktop_col_bot = colors_bot[i];
            return;
        }
    }

    // Mouse Speed
    int sense_map[] = { 1, 2, 4 };
    for (int i = 0; i < 3; i++) {
        int bx = cx + 10 + (i * 60);
        int by = cy + 100;
        if (rect_contains(bx, by, 50, 24, x, y)) {
            mouse_set_sensitivity(sense_map[i]);
            return;
        }
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;

    if (start_menu_open) {
        int my = ty - 280;
        if (x < 160 && y > my) {
            if (rect_contains(0, my+10, 160, 24, x, y)) create_window(APP_TERMINAL, "Terminal", 320, 240);
            else if (rect_contains(0, my+40, 160, 24, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
            else if (rect_contains(0, my+80, 160, 24, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(0, my+110, 160, 24, x, y)) create_window(APP_CALC, "Calculator", 220, 300);
            else if (rect_contains(0, my+150, 160, 24, x, y)) create_window(APP_SETTINGS, "Settings", 250, 200);
            else if (rect_contains(0, my+240, 160, 30, x, y)) outw(0x604, 0x2000);
            start_menu_open = false; return;
        }
        start_menu_open = false;
    }

    if (y >= ty) {
        if (x < 80) { start_menu_open = !start_menu_open; return; }
        int tx = 90;
        for (int i=0; i<MAX_WINDOWS; i++) if(windows[i] && windows[i]->visible) {
            if (rect_contains(tx, ty, 100, TASKBAR_H, x, y)) {
                if(windows[i]->focused && !windows[i]->minimized) windows[i]->minimized = true;
                else focus_window(i);
                return;
            }
            tx += 105;
        }
        return;
    }

    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = windows[i];
        if (w && w->visible && !w->minimized) {
            if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
                int idx = focus_window(i); w = windows[idx];
                int by = w->y+5, cx = w->x+w->w-25;
                if (rect_contains(cx, by, 20, 18, x, y)) { close_window(idx); return; }
                if (rect_contains(cx-22, by, 20, 18, x, y)) { toggle_maximize(w); return; }
                if (rect_contains(cx-44, by, 20, 18, x, y)) { w->minimized = true; return; }
                
                if (y < w->y + WIN_CAPTION_H && !w->maximized) {
                    w->dragging = true; w->drag_off_x = x - w->x; w->drag_off_y = y - w->y; return;
                }
                
                if (w->type == APP_CALC) handle_calc_logic(w, 0); 
                if (w->type == APP_SETTINGS) handle_settings_click(w, x, y);
                if (w->type == APP_FILES) {
                    int ly = w->y + WIN_CAPTION_H + 28; size_t cnt = fs_file_count();
                    for(size_t f=0; f<cnt; f++) {
                        if(rect_contains(w->x+4, ly+(f*18), w->w-8, 18, x, y)) { w->state.files.selected_index=f; return; }
                    }
                }
                return;
            }
        }
    }

    if (rect_contains(20, 20, 64, 55, x, y)) create_window(APP_TERMINAL, "Terminal", 320, 240);
    else if (rect_contains(20, 80, 64, 55, x, y)) create_window(APP_FILES, "File Explorer", 400, 300);
    else if (rect_contains(20, 140, 64, 55, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 200, 64, 55, x, y)) create_window(APP_CALC, "Calc", 220, 300);
}

static void handle_calc_logic(Window* w, char key) {
    if (key == 0) { 
        int cx = w->x + 14; int cy = w->y + WIN_CAPTION_H + 54;
        const char* btns = "789/456*123-C0=+";
        for(int b=0; b<16; b++) {
            if (rect_contains(cx+(b%4)*40, cy+(b/4)*35, 35, 30, mouse.x, mouse.y)) {
                char c = btns[b];
                CalcState* s = &w->state.calc;
                if (c >= '0' && c <= '9') {
                    int d = c - '0';
                    if (s->new_entry) { s->current_val = d; s->new_entry = false; }
                    else if (s->current_val < 100000000) s->current_val = s->current_val * 10 + d;
                } else if (c == 'C') {
                    s->current_val = 0; s->accumulator = 0; s->op = 0; s->new_entry = true;
                } else if (c == '+' || c == '-' || c == '*' || c == '/') {
                    s->accumulator = s->current_val; s->op = c; s->new_entry = true;
                } else if (c == '=') {
                    if (s->op == '+') s->current_val = s->accumulator + s->current_val;
                    else if (s->op == '-') s->current_val = s->accumulator - s->current_val;
                    else if (s->op == '*') s->current_val = s->accumulator * s->current_val;
                    else if (s->op == '/') { if(s->current_val!=0) s->current_val = s->accumulator / s->current_val; }
                    s->op = 0; s->new_entry = true;
                }
            }
        }
    }
}

static void handle_terminal_input(Window* w, char c) {
    TerminalState* ts = &w->state.term;
    if (c == '\n') {
        // Scroll history
        for (int i=0; i<4; i++) str_copy(ts->history[i], ts->history[i+1]);
        str_copy(ts->history[4], ts->input);
        
        // Execute simple commands
        if (kstrcmp(ts->input, "exit") == 0) { close_window(w->id); return; }
        else if (kstrcmp(ts->input, "cls") == 0) {
            for(int i=0; i<5; i++) ts->history[i][0] = 0;
        }
        else if (kstrcmp(ts->input, "help") == 0) {
            for (int i=0; i<4; i++) str_copy(ts->history[i], ts->history[i+1]);
            str_copy(ts->history[4], "cmds: help, cls, exit");
        }
        else if (ts->input_len > 0) {
            for (int i=0; i<4; i++) str_copy(ts->history[i], ts->history[i+1]);
            str_copy(ts->history[4], "Unknown command.");
        }
        
        ts->input[0] = 0;
        ts->input_len = 0;
    } else if (c == '\b') {
        if (ts->input_len > 0) ts->input[--ts->input_len] = 0;
    } else if (c >= 32 && c <= 126 && ts->input_len < 60) {
        ts->input[ts->input_len++] = c;
        ts->input[ts->input_len] = 0;
    }
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

void gui_demo_run(void) {
    syslog_write("GUI: Starting desktop environment...");
    __asm__ volatile("sti");
    graphics_enable_double_buffer();
    screen_w = graphics_get_width(); screen_h = graphics_get_height();
    mouse_init();
    for(int i=0; i<MAX_WINDOWS; i++) windows[i] = NULL;
    create_window(APP_WELCOME, "Welcome", 300, 160);

    bool running = true;
    while(running) {
        timer_wait(1);
        char c = keyboard_poll_char();
        if (c == 27) running = false;
        
        Window* top = get_top_window();
        if (c && top && top->visible && !top->minimized) {
            if (top->type == APP_NOTEPAD) {
                NotepadState* ns = &top->state.notepad;
                if (c == '\b') { if (ns->length > 0) ns->buffer[--ns->length] = 0; }
                else if (c >= 32 && c <= 126 && ns->length < 250) { ns->buffer[ns->length++] = c; ns->buffer[ns->length] = 0; }
            } else if (top->type == APP_TERMINAL) {
                handle_terminal_input(top, c);
            }
        }

        prev_mouse = mouse; mouse = mouse_get_state();
        if (mouse.left_button && top && top->visible && top->dragging) {
            top->x = mouse.x - top->drag_off_x; top->y = mouse.y - top->drag_off_y;
        }
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        if (!mouse.left_button && prev_mouse.left_button) {
            for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) windows[i]->dragging = false;
        }
        render_desktop();
        graphics_swap_buffer();
    }
    for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) kfree(windows[i]);
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
}
