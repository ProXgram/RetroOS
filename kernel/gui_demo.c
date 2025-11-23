#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "io.h"
#include "kstdio.h"
#include <stdbool.h>

// --- Configuration ---
#define MAX_WINDOWS 10
#define WIN_CAPTION_H 24
#define TASKBAR_H 32

// --- Theme Colors ---
#define COL_DESKTOP     0xFF2D73A8
#define COL_TASKBAR     0xFF18334E
#define COL_START_BTN   0xFF1F4E79
#define COL_START_HOVER 0xFF3465A4
#define COL_WIN_ACTIVE  0xFF6B95BD
#define COL_WIN_INACT   0xFF888888
#define COL_WIN_BODY    0xFFF0F0F0
#define COL_BTN_HOVER   0xFF4F81BD
#define COL_BTN_PRESS   0xFF2F518D
#define COL_WHITE       0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_RED         0xFFE81123

// --- Types ---
typedef enum { APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC } AppType;

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
    int id;
    AppType type;
    char title[32];
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool focused;
    bool dragging;
    int drag_off_x, drag_off_y;
    union { CalcState calc; NotepadState notepad; } state;
} Window;

// --- State ---
static Window windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;
static bool needs_redraw = true;

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

static void focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return;
    Window temp = windows[index];
    // Shift others down
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
    }
    windows[MAX_WINDOWS - 1] = temp;
    
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;
    windows[MAX_WINDOWS - 1].focused = true;
    windows[MAX_WINDOWS - 1].minimized = false;
    needs_redraw = true;
}

static Window* create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) { slot = i; break; }
    }
    if (slot == -1) slot = 0; // recycle bottom

    Window* win = &windows[slot];
    win->id = slot;
    win->type = type;
    str_copy(win->title, title);
    win->w = w; win->h = h;
    win->x = (screen_w - w)/2 + (slot*20);
    win->y = (screen_h - h)/2 + (slot*20);
    win->visible = true;
    win->minimized = false;
    win->dragging = false;
    
    if (type == APP_NOTEPAD) {
        win->state.notepad.length = 0;
        win->state.notepad.buffer[0] = 0;
    } else if (type == APP_CALC) {
        win->state.calc.current_val = 0;
        win->state.calc.new_entry = true;
    }

    focus_window(slot);
    return &windows[MAX_WINDOWS - 1];
}

// --- Drawing ---
static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    graphics_fill_rect(x, y, w, h, col);
}

static void draw_bevel(int x, int y, int w, int h, uint32_t fill, bool sunk) {
    draw_rect(x, y, w, h, fill);
    uint32_t top = sunk ? 0xFF555555 : 0xFFFFFFFF;
    uint32_t bot = sunk ? 0xFFFFFFFF : 0xFF555555;
    draw_rect(x, y, w, 1, top);
    draw_rect(x, y, 1, h, top);
    draw_rect(x, y+h-1, w, 1, bot);
    draw_rect(x+w-1, y, 1, h, bot);
}

static void draw_button(int x, int y, int w, int h, const char* lbl, bool press, bool hover) {
    uint32_t col = press ? COL_BTN_PRESS : (hover ? COL_BTN_HOVER : 0xFFDDDDDD);
    draw_bevel(x, y, w, h, col, press);
    int tw = kstrlen(lbl) * 8;
    int tx = x + (w - tw)/2;
    int ty = y + (h - 8)/2;
    if (press) { tx++; ty++; }
    uint32_t tcol = (press || hover) ? COL_WHITE : COL_BLACK;
    graphics_draw_string_scaled(tx, ty, lbl, tcol, col, 1);
}

static void render_notepad(Window* w, int cx, int cy) {
    draw_bevel(cx+2, cy+2, w->w-4, w->h-WIN_CAPTION_H-4, COL_WHITE, true);
    graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
    if ((timer_get_ticks() / 25) % 2) {
        int cur_x = cx + 6 + (w->state.notepad.length * 8);
        draw_rect(cur_x, cy+6, 2, 10, COL_BLACK);
    }
}

static void render_calc(Window* w, int cx, int cy) {
    char buf[16];
    int_to_str(w->state.calc.current_val, buf);
    draw_bevel(cx+10, cy+10, w->w-20, 25, COL_WHITE, true);
    graphics_draw_string_scaled(cx+w->w-20-(kstrlen(buf)*8), cy+18, buf, COL_BLACK, COL_WHITE, 1);

    const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    int bx = cx+10, by = cy+45;
    for(int i=0; i<16; i++) {
        int r=i/4, c=i%4;
        int b_x = bx + c*35;
        int b_y = by + r*30;
        bool hover = rect_contains(b_x, b_y, 30, 25, mouse.x, mouse.y);
        bool press = hover && mouse.left_button;
        draw_button(b_x, b_y, 30, 25, btns[i], press, hover);
    }
}

static void render_window(Window* w) {
    if (!w->visible || w->minimized) return;
    uint32_t fc = w->focused ? COL_WIN_ACTIVE : COL_WIN_INACT;
    
    // Frame
    draw_rect(w->x+6, w->y+6, w->w, w->h, 0x40000000); // Shadow
    draw_rect(w->x, w->y, w->w, w->h, fc);
    draw_rect(w->x+2, w->y+2, w->w-4, w->h-4, fc);
    
    // Title
    draw_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H-3, fc);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_BLACK, fc, 1);
    
    // Controls
    int bx = w->x + w->w - 24;
    int by = w->y + 5;
    bool hov_c = rect_contains(bx, by, 18, 18, mouse.x, mouse.y);
    draw_rect(bx, by, 18, 18, hov_c ? COL_RED : 0xFFCC8888);
    graphics_draw_char(bx+5, by+5, 'X', COL_WHITE, hov_c ? COL_RED : 0xFFCC8888);
    
    // Client
    int cx = w->x+4, cy = w->y+WIN_CAPTION_H+4;
    draw_rect(cx, cy, w->w-8, w->h-WIN_CAPTION_H-8, COL_WIN_BODY);
    
    if (w->type == APP_NOTEPAD) render_notepad(w, cx, cy);
    else if (w->type == APP_CALC) render_calc(w, cx, cy);
    else {
        graphics_draw_string_scaled(cx+10, cy+20, "Welcome to Nostalux!", COL_BLACK, COL_WIN_BODY, 1);
    }
}

static void render_desktop(void) {
    // Background
    draw_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
    
    // Icons
    bool h1 = rect_contains(20, 20, 60, 30, mouse.x, mouse.y);
    graphics_draw_string_scaled(20, 20, "My PC", h1 ? COL_WHITE : 0xFFDDDDDD, COL_DESKTOP, 1);
    
    bool h2 = rect_contains(20, 60, 60, 30, mouse.x, mouse.y);
    graphics_draw_string_scaled(20, 60, "Notepad", h2 ? COL_WHITE : 0xFFDDDDDD, COL_DESKTOP, 1);
    
    bool h3 = rect_contains(20, 100, 60, 30, mouse.x, mouse.y);
    graphics_draw_string_scaled(20, 100, "Calc", h3 ? COL_WHITE : 0xFFDDDDDD, COL_DESKTOP, 1);

    // Windows
    for (int i = 0; i < MAX_WINDOWS; i++) render_window(&windows[i]);

    // Taskbar
    int ty = screen_h - TASKBAR_H;
    draw_rect(0, ty, screen_w, TASKBAR_H, COL_TASKBAR);
    draw_rect(0, ty, screen_w, 2, 0xFF607080);
    
    // Start Btn
    bool h_start = rect_contains(0, ty, 80, TASKBAR_H, mouse.x, mouse.y);
    uint32_t scol = (h_start || start_menu_open) ? COL_START_HOVER : COL_START_BTN;
    draw_rect(0, ty, 80, TASKBAR_H, scol);
    graphics_draw_string_scaled(15, ty+10, "Start", COL_WHITE, scol, 1);
    
    // Taskbar Items
    int bx = 90;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if (windows[i].visible) {
            bool active = windows[i].focused && !windows[i].minimized;
            uint32_t bcol = active ? 0xFF3A6EA5 : 0xFF2A4E75;
            draw_bevel(bx, ty+2, 100, TASKBAR_H-4, bcol, active);
            
            char buf[11]; int k=0; while(k<10 && windows[i].title[k]){buf[k]=windows[i].title[k]; k++;} buf[k]=0;
            graphics_draw_string_scaled(bx+5, ty+10, buf, COL_WHITE, bcol, 1);
            bx += 105;
        }
    }
    
    // Start Menu
    if (start_menu_open) {
        int my = ty - 200;
        draw_bevel(0, my, 160, 200, COL_WIN_BODY, false);
        bool h_cal = rect_contains(0, my+10, 160, 20, mouse.x, mouse.y);
        graphics_draw_string_scaled(10, my+10, "Calculator", h_cal?COL_BTN_HOVER:COL_BLACK, COL_WIN_BODY, 1);
        bool h_not = rect_contains(0, my+40, 160, 20, mouse.x, mouse.y);
        graphics_draw_string_scaled(10, my+40, "Notepad", h_not?COL_BTN_HOVER:COL_BLACK, COL_WIN_BODY, 1);
        
        bool h_shut = rect_contains(0, my+170, 160, 20, mouse.x, mouse.y);
        graphics_draw_string_scaled(10, my+170, "Shutdown", h_shut?COL_RED:COL_BLACK, COL_WIN_BODY, 1);
    }

    // Cursor
    int mx = mouse.x, my = mouse.y;
    draw_rect(mx, my, 2, 14, COL_BLACK);
    draw_rect(mx, my, 12, 2, COL_BLACK);
    draw_rect(mx+2, my+2, 8, 8, COL_WHITE);
}

// --- Inputs ---
static void handle_calc_logic(Window* w, char c) {
    CalcState* s = &w->state.calc;
    if (c >= '0' && c <= '9') {
        int d = c - '0';
        if (s->new_entry) { s->current_val = d; s->new_entry = false; }
        else if (s->current_val < 10000000) s->current_val = s->current_val * 10 + d;
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

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;
    
    // Start Menu
    if (start_menu_open) {
        int my = ty - 200;
        if (rect_contains(0, my, 160, 200, x, y)) {
            if (y > my + 160) outw(0x604, 0x2000); // Shutdown
            else if (y > my + 30 && y < my + 60) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (y < my + 30) create_window(APP_CALC, "Calculator", 200, 250);
            start_menu_open = false; needs_redraw = true; return;
        }
        start_menu_open = false; needs_redraw = true; // click outside
    }

    // Taskbar
    if (y >= ty) {
        if (x < 80) { start_menu_open = !start_menu_open; needs_redraw = true; return; }
        int bx = 90;
        for(int i=0; i<MAX_WINDOWS; i++) {
            if (windows[i].visible) {
                if (rect_contains(bx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i].focused && !windows[i].minimized) windows[i].minimized = true;
                    else { windows[i].minimized = false; focus_window(i); }
                    needs_redraw = true; return;
                }
                bx += 105;
            }
        }
        return;
    }

    // Windows (Top Down)
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = &windows[i];
        if (!w->visible || w->minimized) continue;
        if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
            focus_window(i);
            w = &windows[MAX_WINDOWS - 1]; // ptr updated
            
            // Controls
            if (rect_contains(w->x + w->w - 24, w->y + 4, 18, 18, x, y)) { w->visible = false; needs_redraw=true; return; }
            
            // Drag
            if (y < w->y + WIN_CAPTION_H) {
                w->dragging = true;
                w->drag_off_x = x - w->x;
                w->drag_off_y = y - w->y;
                return;
            }
            
            // Calc Buttons
            if (w->type == APP_CALC) {
                const char* btns = "789/456*123-C0=+";
                int bx = w->x + 14, by = w->y + WIN_CAPTION_H + 49;
                for(int b=0; b<16; b++) {
                    int r=b/4, c=b%4;
                    if (rect_contains(bx + c*35, by + r*30, 30, 25, x, y)) {
                        handle_calc_logic(w, btns[b]); needs_redraw=true; return;
                    }
                }
            }
            return;
        }
    }

    // Desktop Icons
    if (rect_contains(20, 20, 60, 30, x, y)) create_window(APP_WELCOME, "My PC", 300, 150);
    if (rect_contains(20, 60, 60, 30, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    if (rect_contains(20, 100, 60, 30, x, y)) create_window(APP_CALC, "Calc", 200, 250);
}

void gui_demo_run(void) {
    syslog_write("GUI: Starting...");
    mouse_init();
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    create_window(APP_WELCOME, "Welcome", 300, 160);
    needs_redraw = true;

    bool running = true;
    while(running) {
        // Key
        char c = keyboard_poll_char();
        if (c == 27) running = false;
        if (c && windows[MAX_WINDOWS-1].focused && windows[MAX_WINDOWS-1].type == APP_NOTEPAD) {
            Window* w = &windows[MAX_WINDOWS-1];
            if(c=='\b' && w->state.notepad.length>0) w->state.notepad.buffer[--w->state.notepad.length]=0;
            else if(c>=32 && w->state.notepad.length<250) {
                w->state.notepad.buffer[w->state.notepad.length++] = c;
                w->state.notepad.buffer[w->state.notepad.length] = 0;
            }
            needs_redraw = true;
        }

        // Mouse
        prev_mouse = mouse;
        mouse = mouse_get_state();
        
        bool move = (mouse.x != prev_mouse.x || mouse.y != prev_mouse.y);
        bool click = (mouse.left_button && !prev_mouse.left_button);
        bool release = (!mouse.left_button && prev_mouse.left_button);

        if (click) { on_click(mouse.x, mouse.y); needs_redraw = true; }
        if (release) { 
            for(int i=0; i<MAX_WINDOWS; i++) windows[i].dragging = false; 
            needs_redraw = true;
        }
        if (move) needs_redraw = true;

        // Dragging
        if (mouse.left_button) {
            Window* top = &windows[MAX_WINDOWS-1];
            if (top->visible && top->dragging) {
                top->x = mouse.x - top->drag_off_x;
                top->y = mouse.y - top->drag_off_y;
                needs_redraw = true;
            }
        }

        if (needs_redraw) {
            render_desktop();
            needs_redraw = false;
        }
        
        // Blink cursor timer check
        static int last_blink = 0;
        if (timer_get_ticks() > last_blink + 25) {
            last_blink = timer_get_ticks();
            needs_redraw = true; // Trigger blinking cursors
        }

        timer_wait(1);
    }
    
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
}
