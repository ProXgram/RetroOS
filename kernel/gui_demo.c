#include "gui_demo.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "syslog.h"
#include "kstring.h"
#include "kstdio.h"
#include "fs.h"
#include "system.h"
#include "heap.h"
#include <stdbool.h>

// --- SYSCALL WRAPPERS ---

static void syscall_yield(void) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)0) : "memory");
}

static void syscall_exit(void) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)1) : "memory");
    while(1);
}

static void syscall_shutdown(void) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)4) : "memory");
}

static void syscall_get_mouse(MouseState* out) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)5), "S"(out) : "memory");
}

static void* syscall_malloc(size_t size) {
    uint64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "D"((uint64_t)6), "S"(size) : "memory");
    return (void*)ret;
}

static void syscall_free(void* ptr) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)7), "S"(ptr) : "memory");
}

static void syscall_get_time(char* buf) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)8), "S"(buf) : "memory");
}

// --- Global State & Config ---
static volatile bool g_gui_running = false;

bool gui_is_running(void) { return g_gui_running; }
void gui_set_running(bool running) { g_gui_running = running; }

#define MAX_WINDOWS 16
#define WIN_CAPTION_H 28
#define TASKBAR_H 40
#define RESIZE_HANDLE 12

// Theme Structure
typedef struct {
    uint32_t desktop;
    uint32_t taskbar;
    uint32_t win_body;
    uint32_t win_title_active;
    uint32_t win_title_inactive;
    uint32_t win_border;
    bool is_glass;
} Theme;

static Theme themes[] = {
    // 0: Ocean Glass
    { 0xFF004488, 0xAA101010, 0xFFF0F0F0, 0xFF003366, 0xFF505050, 0xFF000000, true },
    // 1: Retro Grey
    { 0xFF008080, 0xFFC0C0C0, 0xFFC0C0C0, 0xFF000080, 0xFF808080, 0xFFFFFFFF, false }
};
static int current_theme_idx = 0;

// --- App Types ---
typedef enum { 
    APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC, APP_FILES, 
    APP_SETTINGS, APP_TERMINAL, APP_BROWSER, APP_TASKMGR, APP_PAINT,
    APP_MINESWEEPER, APP_SYSMON, APP_RUN
} AppType;

// --- App States ---
typedef struct { int current_val; int accumulator; char op; bool new_entry; } CalcState;
typedef struct { char buffer[512]; int length; } NotepadState;
typedef struct { int selected_index; int scroll_offset; } FileManagerState;
typedef struct { bool wallpaper_enabled; } SettingsState;
typedef struct { char prompt[16]; char input[64]; int input_len; char history[6][64]; } TerminalState;
typedef struct { char url[64]; int url_len; char status[32]; } BrowserState;
typedef struct { int selected_pid; } TaskMgrState;
typedef struct { uint32_t* canvas_buffer; int width; int height; uint32_t current_color; int brush_size; } PaintState;
typedef struct { char cmd[32]; int len; } RunState;

// Minesweeper
#define MINE_GRID_W 10
#define MINE_GRID_H 10
typedef struct {
    uint8_t grid[MINE_GRID_H][MINE_GRID_W]; // 9=mine, 0-8=neighbors
    uint8_t view[MINE_GRID_H][MINE_GRID_W]; // 0=covered, 1=revealed, 2=flag
    bool game_over;
    bool victory;
    int flags_placed;
} MineState;

// System Monitor
#define SYSMON_HIST 60
typedef struct {
    int cpu_hist[SYSMON_HIST];
    int mem_hist[SYSMON_HIST];
    int head;
    int update_tick;
} SysMonState;

typedef struct {
    int id; AppType type; char title[32]; 
    int x, y, w, h;
    int min_w, min_h;
    bool visible, minimized, maximized, focused, dragging, resizing;
    int drag_off_x, drag_off_y;
    int restore_x, restore_y, restore_w, restore_h;
    union { 
        CalcState calc; NotepadState notepad; FileManagerState files; 
        SettingsState settings; TerminalState term; BrowserState browser;
        TaskMgrState taskmgr; PaintState paint; MineState mine;
        SysMonState sysmon; RunState run;
    } state;
} Window;

static Window* windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;
static bool g_wallpaper_enabled = false;

// Pseudo-random
static unsigned long rand_state = 1234;
static int fast_rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (unsigned int)(rand_state / 65536) % 32768;
}

// --- Helpers ---
static int kstrcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
static int kstrlen_local(const char* s) { int l=0; while(s[l]) l++; return l; }
static void str_copy(char* d, const char* s) { int i=0; while(s[i] && i<63) {d[i]=s[i]; i++;} d[i]=0; }
static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}
static void int_to_str(int v, char* buf) {
    if(v==0){buf[0]='0';buf[1]=0;return;}
    bool n=v<0; if(n)v=-v;
    int i=0; char t[16]; while(v>0){t[i++]='0'+(v%10);v/=10;}
    if(n)t[i++]='-';
    int j=0; while(i>0)buf[j++]=t[--i]; buf[j]=0;
}

// --- Window Management ---
static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    if (windows[index]->type == APP_PAINT && windows[index]->state.paint.canvas_buffer)
        syscall_free(windows[index]->state.paint.canvas_buffer);
    syscall_free(windows[index]);
    windows[index] = NULL;
}

static int focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return -1;
    Window* t = windows[index];
    // Move to top of list (highest index)
    for(int i=index; i<MAX_WINDOWS-1; i++) {
        windows[i] = windows[i+1];
        if(windows[i]) windows[i]->id = i;
    }
    int top = MAX_WINDOWS-1;
    while(top>0 && windows[top]==NULL) top--; // Find first empty from end? No, shift logic needs care.
    // Re-scan to find first NULL slot or last slot
    top = 0; while(top < MAX_WINDOWS && windows[top] != NULL) top++;
    if (top > 0) top--; // The slot we just cleared is at end of valid list
    
    // Actually, the shifting above creates a NULL at the end of the list of valid windows
    // So we place 't' at the end of valid windows.
    windows[top] = t; t->id = top;
    
    for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) {
        windows[i]->focused = (windows[i] == t);
        if(windows[i]->focused) windows[i]->minimized = false;
    }
    return top;
}

static void create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    for(int i=0; i<MAX_WINDOWS; i++) if(!windows[i]) { slot=i; break; }
    if(slot == -1) { close_window(0); slot=MAX_WINDOWS-1; while(slot>0 && windows[slot]) slot--; }

    Window* win = (Window*)syscall_malloc(sizeof(Window));
    if(!win) return;
    
    win->id = slot; win->type = type; str_copy(win->title, title);
    win->w = w; win->h = h; win->min_w = 150; win->min_h = 100;
    win->x = 40+(slot*20); win->y = 40+(slot*20);
    if(win->x+w > screen_w) win->x=20; if(win->y+h > screen_h-TASKBAR_H) win->y=20;
    
    win->visible = true; win->focused = true;
    win->minimized = false; win->maximized = false;
    win->dragging = false; win->resizing = false;

    // Initialize Apps
    if (type == APP_PAINT) {
        win->min_w = 200; win->min_h = 200;
        int cw = w-12; int ch = h-WIN_CAPTION_H-12;
        win->state.paint.width = cw; win->state.paint.height = ch;
        win->state.paint.canvas_buffer = syscall_malloc(cw*ch*4);
        win->state.paint.current_color = 0xFF000000; win->state.paint.brush_size = 2;
        if(win->state.paint.canvas_buffer) {
            for(int i=0; i<cw*ch; i++) win->state.paint.canvas_buffer[i] = 0xFFFFFFFF;
        }
    } else if (type == APP_SYSMON) {
        for(int i=0; i<SYSMON_HIST; i++) {
            win->state.sysmon.cpu_hist[i] = 0;
            win->state.sysmon.mem_hist[i] = 0;
        }
        win->state.sysmon.head = 0;
    } else if (type == APP_MINESWEEPER) {
        MineState* ms = &win->state.mine;
        ms->game_over = false; ms->victory = false; ms->flags_placed = 0;
        // Init Grid
        for(int r=0; r<10; r++) for(int c=0; c<10; c++) { ms->grid[r][c] = 0; ms->view[r][c] = 0; }
        // Place 15 mines
        int placed = 0;
        while(placed < 15) {
            int r = fast_rand() % 10; int c = fast_rand() % 10;
            if (ms->grid[r][c] != 9) {
                ms->grid[r][c] = 9; placed++;
                // Update neighbors
                for(int rr=r-1; rr<=r+1; rr++) for(int cc=c-1; cc<=c+1; cc++) {
                    if(rr>=0 && rr<10 && cc>=0 && cc<10 && ms->grid[rr][cc] != 9)
                        ms->grid[rr][cc]++;
                }
            }
        }
    } else if (type == APP_RUN) {
        win->state.run.cmd[0] = 0; win->state.run.len = 0;
    } else if (type == APP_TERMINAL) {
        str_copy(win->state.term.prompt, "$ ");
        win->state.term.input[0]=0; win->state.term.input_len=0;
        for(int k=0; k<6; k++) win->state.term.history[k][0] = 0;
    } else if (type == APP_SETTINGS) {
        win->state.settings.wallpaper_enabled = g_wallpaper_enabled;
    } else if (type == APP_BROWSER) {
        str_copy(win->state.browser.url, "www.retro-os.net");
        win->state.browser.url_len = kstrlen_local("www.retro-os.net");
    }

    windows[slot] = win;
    focus_window(slot);
}

// --- Logic ---

static void handle_minesweeper(Window* w, int rx, int ry, bool right_click) {
    MineState* ms = &w->state.mine;
    if (ms->game_over) {
        // Reset on click
        create_window(APP_MINESWEEPER, "Minesweeper", 220, 260); // Create new, let old close or just reset
        close_window(w->id);
        return;
    }
    
    int grid_x = (w->w - (10*20)) / 2;
    int grid_y = 40;
    if (rx < grid_x || rx >= grid_x + 200 || ry < grid_y || ry >= grid_y + 200) return;
    
    int c = (rx - grid_x) / 20;
    int r = (ry - grid_y) / 20;
    
    if (right_click) {
        if (ms->view[r][c] == 0) { ms->view[r][c] = 2; ms->flags_placed++; }
        else if (ms->view[r][c] == 2) { ms->view[r][c] = 0; ms->flags_placed--; }
    } else {
        if (ms->view[r][c] == 0) {
            ms->view[r][c] = 1;
            if (ms->grid[r][c] == 9) {
                ms->game_over = true; ms->victory = false;
            } else if (ms->grid[r][c] == 0) {
                // Flood fill (simple non-recursive for stack safety - rudimentary)
                // Just clearing immediate neighbors for this demo
                for(int rr=r-1; rr<=r+1; rr++) for(int cc=c-1; cc<=c+1; cc++)
                    if(rr>=0&&rr<10&&cc>=0&&cc<10 && ms->view[rr][cc]==0) ms->view[rr][cc]=1;
            }
        }
    }
}

static void handle_run_command(Window* w) {
    const char* cmd = w->state.run.cmd;
    if (kstrcmp(cmd, "calc") == 0) create_window(APP_CALC, "Calculator", 220, 300);
    else if (kstrcmp(cmd, "term") == 0) create_window(APP_TERMINAL, "Terminal", 400, 300);
    else if (kstrcmp(cmd, "paint") == 0) create_window(APP_PAINT, "Paint", 500, 400);
    else if (kstrcmp(cmd, "sys") == 0) create_window(APP_SYSMON, "System Monitor", 300, 200);
    else if (kstrcmp(cmd, "mine") == 0) create_window(APP_MINESWEEPER, "Minesweeper", 220, 260);
    else if (kstrcmp(cmd, "browser") == 0) create_window(APP_BROWSER, "Browser", 500, 400);
    else if (kstrcmp(cmd, "exit") == 0) syscall_shutdown();
    close_window(w->id);
}

static void update_sysmon(Window* w) {
    SysMonState* s = &w->state.sysmon;
    s->update_tick++;
    if (s->update_tick % 5 == 0) {
        s->head = (s->head + 1) % SYSMON_HIST;
        // Fake data
        int cpu = (fast_rand() % 40) + (fast_rand() % 40); // 0-80 roughly
        int mem = 20 + (fast_rand() % 10);
        s->cpu_hist[s->head] = cpu;
        s->mem_hist[s->head] = mem;
    }
}

// --- Drawing ---

static void graphics_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i = 0; i < h; i++) {
        // Simple switch at half
        graphics_fill_rect(x, y+i, w, 1, (i < h/2) ? c1 : c2);
    }
}

static void draw_window(Window* w) {
    Theme* t = &themes[current_theme_idx];
    
    // Drop Shadow (Simulated)
    if (!w->maximized) graphics_fill_rect_alpha(w->x+6, w->y+6, w->w, w->h, 0x000000, 60);

    // Border
    if (t->is_glass) {
        // Glass effect: Translucent border
        graphics_fill_rect_alpha(w->x-2, w->y-2, w->w+4, w->h+4, t->win_border, 100);
        graphics_fill_rect(w->x, w->y, w->w, w->h, t->win_body);
    } else {
        // Solid Retro Border
        graphics_fill_rect(w->x-2, w->y-2, w->w+4, w->h+4, 0xFFC0C0C0);
        graphics_fill_rect(w->x, w->y, w->w, w->h, t->win_body);
        // 3D look
        graphics_fill_rect(w->x-2, w->y-2, w->w+4, 2, 0xFFFFFFFF);
        graphics_fill_rect(w->x-2, w->y-2, 2, w->h+4, 0xFFFFFFFF);
        graphics_fill_rect(w->x+w->w, w->y-2, 2, w->h+4, 0xFF404040);
        graphics_fill_rect(w->x-2, w->y+w->h, w->w+4, 2, 0xFF404040);
    }

    // Title Bar
    uint32_t tc = w->focused ? t->win_title_active : t->win_title_inactive;
    if (t->is_glass) {
        graphics_fill_rect_gradient(w->x, w->y, w->w, WIN_CAPTION_H, tc, tc + 0x00202020);
    } else {
        graphics_fill_rect(w->x+2, w->y+2, w->w-4, WIN_CAPTION_H-2, tc);
    }
    graphics_draw_string_scaled(w->x+8, w->y+6, w->title, COL_WHITE, tc, 1);

    // Controls (Close)
    int bx = w->x + w->w - 24;
    graphics_fill_rect(bx, w->y+4, 18, 18, 0xFFCC3333);
    graphics_draw_char(bx+6, w->y+9, 'X', COL_WHITE, 0xFFCC3333);

    // Content Area
    int cx = w->x + 4; int cy = w->y + WIN_CAPTION_H + 2;
    int cw = w->w - 8; int ch = w->h - WIN_CAPTION_H - 6;
    graphics_fill_rect(cx, cy, cw, ch, t->win_body);

    // App Content
    if (w->type == APP_SYSMON) {
        update_sysmon(w);
        graphics_fill_rect(cx, cy, cw, ch, 0xFF000000);
        // Grid lines
        for(int i=0; i<cw; i+=20) graphics_fill_rect(cx+i, cy, 1, ch, 0xFF003300);
        for(int i=0; i<ch; i+=20) graphics_fill_rect(cx, cy+i, cw, 1, 0xFF003300);
        // Plot CPU
        SysMonState* s = &w->state.sysmon;
        int prev_x = 0; int prev_y = ch;
        for (int i=0; i<SYSMON_HIST; i++) {
            int idx = (s->head - i + SYSMON_HIST) % SYSMON_HIST;
            int val = s->cpu_hist[idx];
            int x = cw - (i * (cw / SYSMON_HIST));
            int y = ch - (val * ch / 100);
            if (i>0) {
                // Draw line (dots for simplicity)
                int dx = x - prev_x; int dy = y - prev_y;
                int steps = (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy) ? (dx > 0 ? dx : -dx) : (dy > 0 ? dy : -dy);
                if (steps == 0) steps = 1;
                for(int k=0; k<steps; k++) 
                    graphics_put_pixel(cx + prev_x + (dx*k/steps), cy + prev_y + (dy*k/steps), 0xFF00FF00);
            }
            prev_x = x; prev_y = y;
        }
        graphics_draw_string_scaled(cx+4, cy+4, "CPU Usage", 0xFF00FF00, 0, 1);
    } 
    else if (w->type == APP_MINESWEEPER) {
        MineState* ms = &w->state.mine;
        int gx = (cw - 200)/2; int gy = 20;
        for(int r=0; r<10; r++) {
            for(int c=0; c<10; c++) {
                int px = cx + gx + c*20; int py = cy + gy + r*20;
                uint32_t col = 0xFFC0C0C0;
                if (ms->view[r][c] == 1) col = 0xFF808080; // Revealed
                
                // Cell bg
                graphics_fill_rect(px, py, 19, 19, col);
                
                if (ms->view[r][c] == 2) { // Flag
                    graphics_fill_rect(px+5, py+5, 10, 10, 0xFFFF0000);
                } else if (ms->view[r][c] == 1) {
                    if (ms->grid[r][c] == 9) { // Mine
                        graphics_fill_rect(px+5, py+5, 10, 10, 0xFF000000);
                    } else if (ms->grid[r][c] > 0) {
                        char n = '0' + ms->grid[r][c];
                        graphics_draw_char(px+6, py+6, n, 0xFF0000FF, col);
                    }
                }
            }
        }
        char status[32];
        if (ms->game_over) str_copy(status, "GAME OVER");
        else { str_copy(status, "Flags: "); char n[4]; int_to_str(ms->flags_placed, n); int len=kstrlen_local(status); int k=0; while(n[k]){status[len++]=n[k++];} status[len]=0; }
        graphics_draw_string_scaled(cx+10, cy+ch-20, status, COL_BLACK, t->win_body, 1);
    }
    else if (w->type == APP_RUN) {
        graphics_draw_string_scaled(cx+10, cy+10, "Type command (calc, term, paint...):", COL_BLACK, t->win_body, 1);
        graphics_fill_rect(cx+10, cy+30, cw-20, 24, COL_WHITE);
        graphics_draw_string_scaled(cx+14, cy+38, w->state.run.cmd, COL_BLACK, COL_WHITE, 1);
        // Blinking cursor
        if ((timer_get_ticks()/20)%2) {
            int cl = kstrlen_local(w->state.run.cmd)*8;
            graphics_fill_rect(cx+14+cl, cy+38, 2, 8, COL_BLACK);
        }
        graphics_fill_rect(cx+cw-60, cy+ch-30, 50, 24, 0xFFDDDDDD);
        graphics_draw_string_scaled(cx+cw-50, cy+ch-22, "Run", COL_BLACK, 0xFFDDDDDD, 1);
    }
    else if (w->type == APP_PAINT) {
        int th = 30;
        graphics_fill_rect(cx, cy, cw, th, 0xFFDDDDDD);
        // Colors
        uint32_t p[] = {0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFFFFFF};
        for(int i=0; i<6; i++) {
            graphics_fill_rect(cx+5+i*25, cy+5, 20, 20, p[i]);
            if (w->state.paint.current_color == p[i]) graphics_fill_rect(cx+5+i*25, cy+26, 20, 2, COL_BLACK);
        }
        
        if (w->state.paint.canvas_buffer) {
            uint32_t* b = w->state.paint.canvas_buffer;
            int bw = w->state.paint.width;
            for(int py=0; py < ch-th; py++) {
                for(int px=0; px < cw; px++) {
                    if (px < bw) graphics_put_pixel(cx+px, cy+th+py, b[py*bw + px]);
                }
            }
        }
    }
    // Default placeholder for others
    else {
        graphics_draw_string_scaled(cx+10, cy+10, "App Running...", 0xFF888888, t->win_body, 1);
        if(w->type == APP_TERMINAL) {
            graphics_fill_rect(cx, cy, cw, ch, COL_BLACK);
            graphics_draw_string_scaled(cx+4, cy+4, w->state.term.prompt, 0xFF00FF00, COL_BLACK, 1);
            graphics_draw_string_scaled(cx+4+(kstrlen_local(w->state.term.prompt)*8), cy+4, w->state.term.input, COL_WHITE, COL_BLACK, 1);
        }
        if(w->type == APP_SETTINGS) {
            graphics_draw_string_scaled(cx+20, cy+20, "Theme:", COL_BLACK, t->win_body, 1);
            graphics_fill_rect(cx+80, cy+15, 100, 24, 0xFFCCCCCC);
            graphics_draw_string_scaled(cx+90, cy+22, t->is_glass ? "Ocean Glass" : "Retro Grey", COL_BLACK, 0xFFCCCCCC, 1);
            
            graphics_draw_string_scaled(cx+20, cy+60, "Wallpaper:", COL_BLACK, t->win_body, 1);
            graphics_fill_rect(cx+110, cy+55, 80, 24, w->state.settings.wallpaper_enabled ? 0xFF88FF88 : 0xFFFF8888);
            graphics_draw_string_scaled(cx+120, cy+62, w->state.settings.wallpaper_enabled ? "ON" : "OFF", COL_BLACK, 0, 1);
        }
    }

    // Resize Grip
    graphics_fill_rect(w->x + w->w - RESIZE_HANDLE, w->y + w->h - RESIZE_HANDLE, RESIZE_HANDLE, RESIZE_HANDLE, 0xFF888888);
}

static void render_taskbar(void) {
    Theme* t = &themes[current_theme_idx];
    int ty = screen_h - TASKBAR_H;
    
    if (t->is_glass) graphics_fill_rect_alpha(0, ty, screen_w, TASKBAR_H, t->taskbar, 200);
    else graphics_fill_rect(0, ty, screen_w, TASKBAR_H, t->taskbar);
    
    // Start Button
    uint32_t sb = start_menu_open ? 0xFF004400 : 0xFF006600;
    graphics_fill_rect(2, ty+2, 60, TASKBAR_H-4, sb);
    graphics_draw_string_scaled(10, ty+12, "START", COL_WHITE, sb, 1);
    
    // Windows
    int tx = 70;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if (windows[i] && windows[i]->visible) {
            bool active = windows[i]->focused && !windows[i]->minimized;
            uint32_t bg = active ? 0xFF505050 : 0xFF303030;
            if (!t->is_glass && active) bg = 0xFFFFFFFF;
            if (!t->is_glass && !active) bg = 0xFFC0C0C0;
            
            graphics_fill_rect(tx, ty+2, 100, TASKBAR_H-4, bg);
            uint32_t tc = t->is_glass ? COL_WHITE : COL_BLACK;
            char title[10]; int k=0; while(windows[i]->title[k]&&k<8){title[k]=windows[i]->title[k];k++;} title[k]=0;
            graphics_draw_string_scaled(tx+5, ty+12, title, tc, bg, 1);
            tx += 105;
        }
    }
    
    // Clock
    char time[16]; syscall_get_time(time);
    graphics_draw_string_scaled(screen_w-60, ty+12, time, COL_WHITE, t->taskbar, 1);
}

// --- Input Handling ---

static void handle_click(int x, int y) {
    // Start Menu
    if (start_menu_open) {
        int mh = 300; int my = screen_h - TASKBAR_H - mh;
        if (x < 160 && y > my) {
            // Hit items
            int item = (y - (my+10)) / 30;
            if (item == 0) create_window(APP_BROWSER, "Browser", 500, 400);
            if (item == 1) create_window(APP_TERMINAL, "Terminal", 400, 300);
            if (item == 2) create_window(APP_FILES, "Files", 400, 300);
            if (item == 3) create_window(APP_PAINT, "Paint", 500, 400);
            if (item == 4) create_window(APP_SYSMON, "Sys Monitor", 300, 200);
            if (item == 5) create_window(APP_MINESWEEPER, "Minesweeper", 220, 260);
            if (item == 6) create_window(APP_RUN, "Run...", 300, 120);
            if (item == 7) create_window(APP_SETTINGS, "Settings", 250, 200);
            if (item == 8) syscall_shutdown();
            start_menu_open = false;
            return;
        }
        start_menu_open = false;
    }

    // Taskbar
    if (y > screen_h - TASKBAR_H) {
        if (x < 65) start_menu_open = !start_menu_open;
        else {
            // Task switching
            int tx = 70;
            for(int i=0; i<MAX_WINDOWS; i++) {
                if(windows[i] && windows[i]->visible) {
                    if(x >= tx && x < tx+100) {
                        if(windows[i]->focused && !windows[i]->minimized) windows[i]->minimized=true;
                        else { windows[i]->minimized=false; focus_window(i); }
                        return;
                    }
                    tx+=105;
                }
            }
        }
        return;
    }

    // Windows (Front-to-back)
    for (int i=MAX_WINDOWS-1; i>=0; i--) {
        Window* w = windows[i];
        if (!w || !w->visible || w->minimized) continue;
        
        if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
            int idx = focus_window(i); w = windows[idx]; // w might change ptr due to reorder
            
            // Resize Grip
            if (x > w->x + w->w - RESIZE_HANDLE && y > w->y + w->h - RESIZE_HANDLE) {
                w->resizing = true;
                w->drag_off_x = x - w->w; // Store offset from width
                w->drag_off_y = y - w->h;
                return;
            }

            // Close Button
            if (x > w->x + w->w - 24 && y < w->y + WIN_CAPTION_H) {
                close_window(idx);
                return;
            }

            // Title Bar (Drag)
            if (y < w->y + WIN_CAPTION_H) {
                w->dragging = true;
                w->drag_off_x = x - w->x;
                w->drag_off_y = y - w->y;
                return;
            }
            
            // App Content Clicks
            int rel_x = x - (w->x+4); int rel_y = y - (w->y+WIN_CAPTION_H+2);
            if (w->type == APP_MINESWEEPER) {
                handle_minesweeper(w, rel_x, rel_y, mouse.right_button);
            } 
            else if (w->type == APP_PAINT) {
                // Palette check
                if (rel_y < 30) {
                    int col_idx = (rel_x - 5) / 25;
                    uint32_t p[] = {0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFFFFFF};
                    if (col_idx >= 0 && col_idx < 6) w->state.paint.current_color = p[col_idx];
                } else {
                    // Draw dot immediately
                    int cv_y = 30;
                    if (w->state.paint.canvas_buffer && rel_x >= 0 && rel_x < w->state.paint.width && rel_y >= cv_y) {
                        w->state.paint.canvas_buffer[(rel_y-cv_y)*w->state.paint.width + rel_x] = w->state.paint.current_color;
                    }
                }
            }
            else if (w->type == APP_RUN) {
                if (rect_contains(w->x+w->w-60, w->y+w->h-30, 50, 24, x, y)) {
                    handle_run_command(w);
                }
            }
            else if (w->type == APP_SETTINGS) {
                if (rect_contains(w->x+80, w->y+15+WIN_CAPTION_H+2, 100, 24, x, y)) {
                    current_theme_idx = (current_theme_idx + 1) % 2;
                }
                if (rect_contains(w->x+110, w->y+55+WIN_CAPTION_H+2, 80, 24, x, y)) {
                    g_wallpaper_enabled = !g_wallpaper_enabled;
                    w->state.settings.wallpaper_enabled = g_wallpaper_enabled;
                }
            }
            return;
        }
    }
}

void gui_demo_run(void) {
    g_gui_running = true;
    graphics_enable_double_buffer();
    screen_w = graphics_get_width();
    screen_h = graphics_get_height();
    
    mouse.x = screen_w/2; mouse.y = screen_h/2;
    for(int i=0; i<MAX_WINDOWS; i++) windows[i] = NULL;
    
    // Auto-launch Welcome
    create_window(APP_WELCOME, "Welcome to RetroOS", 300, 150);

    while(1) {
        syscall_yield();
        
        char c = keyboard_poll_char();
        if (c == 27) break; // Escape
        
        Window* top = get_top_window();
        if (c && top) {
            if (top->type == APP_RUN) {
                RunState* r = &top->state.run;
                if (c == '\n') handle_run_command(top);
                else if (c == '\b') { if(r->len>0) r->cmd[--r->len]=0; }
                else if (r->len < 30) { r->cmd[r->len++]=c; r->cmd[r->len]=0; }
            } else if (top->type == APP_TERMINAL) {
                // ... existing terminal logic simplified
                TerminalState* t = &top->state.term;
                if(c=='\b' && t->input_len>0) t->input[--t->input_len]=0;
                else if(c>31 && t->input_len<60) { t->input[t->input_len++]=c; t->input[t->input_len]=0; }
            }
        }

        prev_mouse = mouse;
        syscall_get_mouse(&mouse);
        
        if (mouse.left_button && top) {
            if (top->dragging) {
                top->x = mouse.x - top->drag_off_x;
                top->y = mouse.y - top->drag_off_y;
                // Snap?
                if (top->x < 10) top->x = 0;
                if (top->y < 10) top->y = 0;
            } else if (top->resizing) {
                int nw = mouse.x - top->drag_off_x;
                int nh = mouse.y - top->drag_off_y;
                if (nw < top->min_w) nw = top->min_w;
                if (nh < top->min_h) nh = top->min_h;
                top->w = nw; top->h = nh;
                // Handle paint resize? (Clear canvas for now)
                if(top->type == APP_PAINT && top->state.paint.canvas_buffer) {
                    // Realloc logic omitted for stability, just crop/white space
                    top->state.paint.width = top->w - 12; // Update logical width
                }
            } else if (top->type == APP_PAINT && rect_contains(top->x+6, top->y+WIN_CAPTION_H+36, top->w-12, top->h-WIN_CAPTION_H-12, mouse.x, mouse.y)) {
                // Drag Paint
                int rel_x = mouse.x - (top->x+6);
                int rel_y = mouse.y - (top->y+WIN_CAPTION_H+36); // 30px toolbar + margins
                int w = top->state.paint.width;
                if (rel_x >= 0 && rel_x < w) {
                    top->state.paint.canvas_buffer[rel_y * w + rel_x] = top->state.paint.current_color;
                    // Simple brush 2x2
                    top->state.paint.canvas_buffer[rel_y * w + rel_x + 1] = top->state.paint.current_color;
                    top->state.paint.canvas_buffer[(rel_y+1) * w + rel_x] = top->state.paint.current_color;
                }
            }
        }
        
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        if (!mouse.left_button) for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) {
            windows[i]->dragging = false;
            windows[i]->resizing = false;
        }

        // Draw
        if (g_wallpaper_enabled) {
            // Procedural Wallpaper
            for(int y=0; y<screen_h; y++) {
                uint32_t c = 0xFF000000 | ((y*255/screen_h) << 16); // Red gradient
                graphics_fill_rect(0, y, screen_w, 1, c);
            }
        } else {
            graphics_fill_rect(0, 0, screen_w, screen_h, themes[current_theme_idx].desktop);
        }
        
        // Desktop Icons (Simplified)
        // ... (Draw loop)
        
        // Windows
        for(int i=0; i<MAX_WINDOWS; i++) if(windows[i] && windows[i]->visible) draw_window(windows[i]);
        
        render_taskbar();
        
        if(start_menu_open) {
            int my = screen_h - TASKBAR_H - 300;
            graphics_fill_rect(0, my, 160, 300, 0xFFE0E0E0);
            graphics_fill_rect(0, my, 160, 300, 0xFFE0E0E0);
            const char* items[] = {"Browser", "Terminal", "Files", "Paint", "Sys Monitor", "Minesweeper", "Run...", "Settings", "Shut Down"};
            for(int i=0; i<9; i++) {
                bool h = rect_contains(0, my+10+i*30, 160, 30, mouse.x, mouse.y);
                if(h) graphics_fill_rect(0, my+10+i*30, 160, 30, 0xFF000080);
                graphics_draw_string_scaled(10, my+18+i*30, items[i], h?COL_WHITE:COL_BLACK, h?0xFF000080:0xFFE0E0E0, 1);
            }
        }
        
        // Mouse
        for(int y=0; y<10; y++) for(int x=0; x<10; x++) graphics_put_pixel(mouse.x+x, mouse.y+y, COL_WHITE); // Simple square cursor for speed
        
        graphics_swap_buffer();
    }
    
    for(int i=0; i<MAX_WINDOWS; i++) close_window(i);
    g_gui_running = false;
    syscall_exit();
}
