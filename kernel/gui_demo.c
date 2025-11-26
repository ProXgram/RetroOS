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

static void syscall_log(const char* msg) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)2), "S"(msg) : "memory");
}

// --- Global State & Config ---
static volatile bool g_gui_running = false;

bool gui_is_running(void) { return g_gui_running; }
void gui_set_running(bool running) { g_gui_running = running; }

#define MAX_WINDOWS 16
#define WIN_CAPTION_H 28
#define TASKBAR_H 40
#define RESIZE_HANDLE 16

// Colors
#define COL_DESKTOP     0xFF004488 
#define COL_TASKBAR     0xFF101010
#define COL_WIN_BODY    0xFFF0F0F0
#define COL_WIN_TITLE_1 0xFF003366
#define COL_WIN_TITLE_2 0xFF0055AA
#define COL_WIN_TEXT    0xFF000000
#define COL_BTN_FACE    0xFFDDDDDD
#define COL_BTN_SHADOW  0xFF555555
#define COL_BTN_HILIGHT 0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_WHITE       0xFFFFFFFF
#define COL_ACCENT      0xFF0078D7

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
    APP_MINESWEEPER, APP_SYSMON, APP_RUN, APP_TICTACTOE, APP_IMAGEVIEW
} AppType;

// --- App States ---
typedef struct { int current_val; int accumulator; char op; bool new_entry; } CalcState;
typedef struct { char buffer[512]; int length; } NotepadState;
typedef struct { int selected_index; int scroll_offset; } FileManagerState;
typedef struct { bool wallpaper_enabled; int theme_id; } SettingsState;
typedef struct { char prompt[16]; char input[64]; int input_len; char history[6][64]; } TerminalState;
typedef struct { char url[64]; int url_len; char status[32]; int scroll; } BrowserState;
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

// Tic Tac Toe
typedef struct {
    char board[3][3]; // 0=empty, 1=X, 2=O
    int turn; // 1=X, 2=O
    int winner; // 0=none, 1=X, 2=O, 3=Draw
} TicTacToeState;

// Image Viewer
typedef struct {
    int seed;
    int zoom;
} ImageViewState;

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
        SysMonState sysmon; RunState run; TicTacToeState ttt;
        ImageViewState img;
    } state;
} Window;

// Mouse Trails
#define TRAIL_LEN 10
typedef struct { int x, y; } Point;
static Point mouse_trail[TRAIL_LEN];
static int trail_head = 0;

static Window* windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;
static bool g_wallpaper_enabled = false;
static bool g_desktop_shown_mode = false;

// --- Forward Declarations ---
static void close_window(int index);
static int focus_window(int index);
static void toggle_maximize(Window* w);
static void handle_paint_click(Window* w, int x, int y);
static void handle_settings_click(Window* w, int x, int y);
static void handle_files_click(Window* w, int x, int y);
static void handle_taskmgr_click(Window* w, int x, int y);
static void handle_browser_click(Window* w, int x, int y);
static void handle_calc_logic(Window* w, char key);
static void handle_terminal_input(Window* w, char c);
static void handle_browser_input(Window* w, char c);
static void handle_run_command(Window* w);
static void handle_minesweeper(Window* w, int rx, int ry, bool right_click);
static void handle_tictactoe(Window* w, int x, int y);
static void handle_imageview(Window* w, int x, int y);
static void render_window(Window* w);
static void draw_wallpaper(void);
static void on_click(int x, int y);
static void update_sysmon(Window* w);
static void create_window(AppType type, const char* title, int w, int h);
static Window* get_top_window(void);

// Pseudo-random
static unsigned long rand_state = 1234;
static int fast_rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (unsigned int)(rand_state / 65536) % 32768;
}

// --- Helpers ---
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

// --- BITMAPS (Icons) ---
// Icons are 24x24 palette-indexed.

static const uint8_t CURSOR_BITMAP[19][12] = {
    {1,1,0,0,0,0,0,0,0,0,0,0}, {1,2,1,0,0,0,0,0,0,0,0,0}, {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0}, {1,2,2,2,2,1,0,0,0,0,0,0}, {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0}, {1,2,2,2,2,2,2,2,1,0,0,0}, {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0}, {1,2,2,2,2,2,1,1,1,1,1,1}, {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,1,1,2,2,1,0,0,0,0,0}, {1,1,0,1,2,2,1,0,0,0,0,0}, {0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,0,1,2,2,1,0,0,0,0}, {0,0,0,0,0,1,2,2,1,0,0,0}, {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0}
};

static const uint8_t ICON_TERM[24][24] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,7,7,7,7,7,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,7,7,7,7,7,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const uint8_t ICON_PAINT[24][24] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,6,6,4,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,6,6,6,6,4,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,6,6,6,6,4,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,6,6,6,4,4,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,4,6,6,4,4,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,6,4,4,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,4,4,4,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,5,5,5,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,5,5,5,5,5,1,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,1,5,5,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0,0},
    {0,0,1,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0},
    {0,0,1,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0},
    {0,0,1,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0},
    {0,0,0,1,5,5,5,5,5,5,5,5,5,5,5,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t ICON_BROWSER[24][24] = {
    {0,0,0,0,0,0,0,6,6,6,6,6,6,6,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,6,6,6,6,6,6,6,6,6,6,6,0,0,0,0,0,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,4,4,6,6,6,6,6,0,0,0,0,0,0,0},
    {0,0,0,6,6,6,6,6,4,4,4,4,4,4,6,6,6,6,0,0,0,0,0,0},
    {0,0,6,6,6,6,6,4,4,4,4,4,4,4,4,6,6,6,6,0,0,0,0,0},
    {0,0,6,6,6,6,4,4,4,4,4,4,4,4,4,4,6,6,6,0,0,0,0,0},
    {0,6,6,6,6,4,4,4,4,4,4,4,4,4,4,4,4,6,6,6,0,0,0,0},
    {0,6,6,6,4,4,4,4,6,6,6,6,6,6,4,4,4,4,6,6,0,0,0,0},
    {6,6,6,4,4,4,6,6,6,6,6,6,6,6,6,6,4,4,4,6,6,0,0,0},
    {6,6,6,4,4,6,6,6,6,6,6,6,6,6,6,6,6,4,4,6,6,0,0,0},
    {6,6,6,4,6,6,6,6,6,6,6,6,6,6,6,6,6,6,4,6,6,0,0,0},
    {6,6,6,4,6,6,6,6,6,6,6,6,6,6,6,6,6,6,4,6,6,0,0,0},
    {6,6,6,4,6,6,6,6,6,6,6,6,6,6,6,6,6,6,4,6,6,0,0,0},
    {6,6,6,4,4,6,6,6,6,6,6,6,6,6,6,6,6,4,4,6,6,0,0,0},
    {6,6,6,4,4,4,6,6,6,6,6,6,6,6,6,6,4,4,4,6,6,0,0,0},
    {0,6,6,6,4,4,4,4,6,6,6,6,6,6,4,4,4,4,6,6,0,0,0,0},
    {0,6,6,6,6,4,4,4,4,4,4,4,4,4,4,4,4,6,6,6,0,0,0,0},
    {0,0,6,6,6,6,4,4,4,4,4,4,4,4,4,4,6,6,6,0,0,0,0,0},
    {0,0,6,6,6,6,6,4,4,4,4,4,4,4,4,6,6,6,6,0,0,0,0,0},
    {0,0,0,6,6,6,6,6,4,4,4,4,4,4,6,6,6,6,0,0,0,0,0,0},
    {0,0,0,0,6,6,6,6,6,6,4,4,6,6,6,6,6,0,0,0,0,0,0,0},
    {0,0,0,0,0,6,6,6,6,6,6,6,6,6,6,6,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,6,6,6,6,6,6,6,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t ICON_TASKMGR[24][24] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,7,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,7,7,7,7,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,7,7,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,7,7,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,7,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,7,7,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,7,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,7,7,7,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,7,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,7,7,7,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,7,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,7,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const uint8_t ICON_FOLDER[24][24] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,5,5,5,5,5,5,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,1,0},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0}
};

static const uint8_t ICON_CALC[24][24] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,0,0,0},
    {1,3,1,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,1,3,1,0,0,0},
    {1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,1,4,1,3,1,4,1,3,1,4,1,3,1,8,1,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,1,4,1,3,1,4,1,3,1,4,1,3,1,4,1,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,1,4,1,3,1,4,1,3,1,4,1,3,1,4,1,3,3,3,1,0,0,0},
    {1,3,1,1,1,3,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,3,1,1,1,1,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,1,4,4,4,4,4,1,3,1,4,1,3,1,4,1,3,3,3,1,0,0,0},
    {1,3,1,1,1,1,1,1,1,3,1,1,1,3,1,1,1,3,3,3,1,0,0,0},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,0,0},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t ICON_SET[24][24] = {
    {0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,2,3,3,3,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,2,2,0,0,0,2,3,3,3,2,0,0,0,2,2,0,0,0,0,0},
    {0,0,0,2,3,2,0,0,2,2,3,3,3,2,2,0,0,2,3,2,0,0,0,0},
    {0,0,0,2,3,2,2,2,3,3,3,3,3,3,3,2,2,2,3,2,0,0,0,0},
    {0,0,0,0,2,3,3,3,3,3,3,3,3,3,3,3,3,3,2,0,0,0,0,0},
    {0,0,0,0,0,2,3,3,3,1,1,1,1,1,3,3,3,2,0,0,0,0,0,0},
    {0,0,0,0,0,2,3,3,1,4,4,4,4,4,1,3,3,2,0,0,0,0,0,0},
    {0,0,2,2,2,3,3,3,1,4,4,4,4,4,1,3,3,3,2,2,2,0,0,0},
    {0,2,3,3,3,3,3,3,1,4,4,4,4,4,1,3,3,3,3,3,3,2,0,0},
    {0,2,3,3,3,3,3,3,1,4,4,4,4,4,1,3,3,3,3,3,3,2,0,0},
    {0,2,3,3,3,3,3,3,1,4,4,4,4,4,1,3,3,3,3,3,3,2,0,0},
    {0,2,3,3,3,3,3,3,1,4,4,4,4,4,1,3,3,3,3,3,3,2,0,0},
    {0,2,3,3,3,3,3,3,1,4,4,4,4,4,1,3,3,3,3,3,3,2,0,0},
    {0,0,2,2,2,3,3,3,1,4,4,4,4,4,1,3,3,3,2,2,2,0,0,0},
    {0,0,0,0,0,2,3,3,1,4,4,4,4,4,1,3,3,2,0,0,0,0,0,0},
    {0,0,0,0,0,2,3,3,3,1,1,1,1,1,3,3,3,2,0,0,0,0,0,0},
    {0,0,0,0,2,3,3,3,3,3,3,3,3,3,3,3,3,3,2,0,0,0,0,0},
    {0,0,0,2,3,2,2,2,3,3,3,3,3,3,3,2,2,2,3,2,0,0,0,0},
    {0,0,0,2,3,2,0,0,2,2,3,3,3,2,2,0,0,2,3,2,0,0,0,0},
    {0,0,0,0,2,2,0,0,0,2,3,3,3,2,0,0,0,2,2,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,2,3,3,3,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t ICON_GAME[24][24] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,1,1,1,8,8,8,8,8,8,4,4,8,8,8,8,1,0,0,0},
    {0,0,1,8,1,8,1,8,1,8,8,8,8,4,8,8,4,8,8,8,1,0,0,0},
    {0,0,1,8,1,1,1,1,1,8,8,8,8,4,8,8,4,8,8,8,1,0,0,0},
    {0,0,1,8,1,8,1,8,1,8,8,8,8,8,4,4,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,1,1,1,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,1,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static const uint8_t ICON_IMAGE[24][24] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,5,5,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,5,5,5,5,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,5,5,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,7,7,4,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,7,7,7,7,4,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,4,7,7,7,7,7,7,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,4,7,7,7,7,7,7,7,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,4,7,7,7,7,6,6,7,7,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,4,7,7,7,7,6,6,6,6,7,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,4,7,7,7,7,6,6,6,6,6,6,4,1,0,0},
    {0,1,4,4,4,4,4,4,4,7,7,7,7,6,6,6,6,6,6,6,4,1,0,0},
    {0,1,4,4,4,4,4,4,7,7,7,7,6,6,6,6,6,6,6,6,4,1,0,0},
    {0,1,4,4,4,4,4,7,7,7,7,6,6,6,6,6,6,6,6,6,4,1,0,0},
    {0,1,4,4,4,4,7,7,7,7,6,6,6,6,6,6,6,6,6,6,4,1,0,0},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

// --- Window Management ---

static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    Window* w = windows[index];
    
    if (w->type == APP_PAINT && w->state.paint.canvas_buffer) {
        syscall_free(w->state.paint.canvas_buffer);
    }
    
    syscall_free(w);
    windows[index] = NULL;
    
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
        if (windows[i]) windows[i]->id = i; 
    }
    windows[MAX_WINDOWS - 1] = NULL;
}

static int focus_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return -1;
    Window* target = windows[index];
    
    for (int i = index; i < MAX_WINDOWS - 1; i++) {
        windows[i] = windows[i+1];
        if (windows[i]) windows[i]->id = i;
    }
    windows[MAX_WINDOWS - 1] = NULL;
    
    int top_slot = 0;
    while (top_slot < MAX_WINDOWS && windows[top_slot] != NULL) top_slot++;
    if (top_slot >= MAX_WINDOWS) top_slot = MAX_WINDOWS - 1;

    windows[top_slot] = target; 
    target->id = top_slot;

    for(int j=0; j<MAX_WINDOWS; j++) {
        if(windows[j]) {
            windows[j]->focused = (windows[j] == target);
            if (windows[j]->focused) windows[j]->minimized = false;
        }
    }
    return target->id;
}

static Window* get_top_window(void) {
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) { 
        if (windows[i] != NULL && windows[i]->visible && !windows[i]->minimized) return windows[i]; 
    } 
    return NULL;
}

static void create_window(AppType type, const char* title, int w, int h) {
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) { if (windows[i] == NULL) { slot = i; break; } }
    if (slot == -1) { close_window(0); slot = MAX_WINDOWS - 1; while(slot>0 && windows[slot]) slot--; }

    Window* win = (Window*)syscall_malloc(sizeof(Window));
    if (!win) return;

    win->id = slot; win->type = type; str_copy(win->title, title);
    win->w = w; win->h = h; win->min_w = 150; win->min_h = 100;
    win->x = 40+(slot*20); win->y = 40+(slot*20);
    if(win->x+w > screen_w) win->x=20; 
    if(win->y+h > screen_h-TASKBAR_H) win->y=20;
    
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
    } else if (type == APP_TICTACTOE) {
        win->min_w = 220; win->min_h = 240;
        for(int r=0;r<3;r++) for(int c=0;c<3;c++) win->state.ttt.board[r][c] = 0;
        win->state.ttt.turn = 1; // X starts
        win->state.ttt.winner = 0;
    } else if (type == APP_IMAGEVIEW) {
        win->min_w = 200; win->min_h = 200;
        win->state.img.zoom = 1;
        win->state.img.seed = fast_rand() % 1000;
    } else if (type == APP_SYSMON) {
        for(int i=0; i<SYSMON_HIST; i++) {
            win->state.sysmon.cpu_hist[i] = 0;
            win->state.sysmon.mem_hist[i] = 0;
        }
        win->state.sysmon.head = 0;
    } else if (type == APP_MINESWEEPER) {
        MineState* ms = &win->state.mine;
        ms->game_over = false; ms->victory = false; ms->flags_placed = 0;
        for(int r=0; r<10; r++) for(int c=0; c<10; c++) { ms->grid[r][c] = 0; ms->view[r][c] = 0; }
        int placed = 0;
        while(placed < 15) {
            int r = fast_rand() % 10; int c = fast_rand() % 10;
            if (ms->grid[r][c] != 9) {
                ms->grid[r][c] = 9; placed++;
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

static void toggle_maximize(Window* w) {
    if (w->maximized) { 
        w->x = w->restore_x; w->y = w->restore_y; 
        w->w = w->restore_w; w->h = w->restore_h; 
        w->maximized = false; 
    } else { 
        w->restore_x = w->x; w->restore_y = w->y; 
        w->restore_w = w->w; w->restore_h = w->h; 
        w->x = 0; w->y = 0; w->w = screen_w; w->h = screen_h - TASKBAR_H; 
        w->maximized = true; 
    }
}

static void handle_minesweeper(Window* w, int rx, int ry, bool right_click) {
    MineState* ms = &w->state.mine;
    if (ms->game_over) {
        create_window(APP_MINESWEEPER, "Minesweeper", 220, 260);
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
                for(int rr=r-1; rr<=r+1; rr++) for(int cc=c-1; cc<=c+1; cc++)
                    if(rr>=0&&rr<10&&cc>=0&&cc<10 && ms->view[rr][cc]==0) ms->view[rr][cc]=1;
            }
        }
    }
}

static void handle_tictactoe(Window* w, int x, int y) {
    TicTacToeState* s = &w->state.ttt;
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    
    // Check restart button
    if (s->winner != 0 && rect_contains(cx + 10, cy + 180, 100, 24, x, y)) {
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) s->board[i][j]=0;
        s->turn = 1; s->winner = 0;
        return;
    }
    
    if (s->winner != 0) return;
    
    // Grid hit test
    for (int r=0; r<3; r++) {
        for (int c=0; c<3; c++) {
            int bx = cx + c*60;
            int by = cy + r*60;
            if (rect_contains(bx, by, 55, 55, x, y)) {
                if (s->board[r][c] == 0) {
                    s->board[r][c] = s->turn;
                    // Check win
                    bool win = false;
                    // Row/Col
                    for(int k=0;k<3;k++) {
                        if (s->board[r][k] != s->turn) break;
                        if (k==2) win=true;
                    }
                    if(!win) for(int k=0;k<3;k++) {
                        if (s->board[k][c] != s->turn) break;
                        if (k==2) win=true;
                    }
                    // Diag
                    if(!win && r==c) {
                        if(s->board[0][0]==s->turn && s->board[1][1]==s->turn && s->board[2][2]==s->turn) win=true;
                    }
                    if(!win && r+c==2) {
                        if(s->board[0][2]==s->turn && s->board[1][1]==s->turn && s->board[2][0]==s->turn) win=true;
                    }
                    
                    if(win) s->winner = s->turn;
                    else {
                        // Check draw
                        bool full=true;
                        for(int i=0;i<3;i++) for(int j=0;j<3;j++) if(s->board[i][j]==0) full=false;
                        if(full) s->winner=3;
                        else s->turn = (s->turn==1) ? 2 : 1;
                    }
                }
            }
        }
    }
}

static void handle_imageview(Window* w, int x, int y) {
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    if (rect_contains(cx, cy, w->w-20, w->h-60, x, y)) {
        w->state.img.zoom = (w->state.img.zoom % 4) + 1;
    } else if (rect_contains(cx, w->y + w->h - 30, 80, 20, x, y)) {
        w->state.img.seed = fast_rand() % 1000;
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
    else if (kstrcmp(cmd, "ttt") == 0) create_window(APP_TICTACTOE, "Tic-Tac-Toe", 220, 240);
    else if (kstrcmp(cmd, "img") == 0) create_window(APP_IMAGEVIEW, "Image Viewer", 300, 300);
    else if (kstrcmp(cmd, "exit") == 0) syscall_shutdown();
    close_window(w->id);
}

static void handle_paint_click(Window* w, int x, int y) {
    if (!w->state.paint.canvas_buffer) return;
    
    int cx = w->x + 6;
    int cy = w->y + WIN_CAPTION_H + 46;
    
    if (y < cy) {
        int palette_y = w->y + WIN_CAPTION_H + 11;
        if (y >= palette_y && y < palette_y + 25) {
            int local_x = x - (cx + 5);
            if (local_x >= 0) {
                int idx = local_x / 30;
                if (idx >= 0 && idx < 8) {
                    uint32_t colors[] = {0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF};
                    w->state.paint.current_color = colors[idx];
                }
            }
        }
        return;
    }
    
    int rel_x = x - cx;
    int rel_y = y - cy;
    int cw = w->state.paint.width;
    int ch = w->state.paint.height;
    
    if (rel_x >= 0 && rel_x < cw && rel_y >= 0 && rel_y < ch) {
        int sz = w->state.paint.brush_size;
        uint32_t col = w->state.paint.current_color;
        uint32_t* buf = w->state.paint.canvas_buffer;
        
        for(int dy=-sz; dy<=sz; dy++) {
            for(int dx=-sz; dx<=sz; dx++) {
                int px = rel_x + dx;
                int py = rel_y + dy;
                if (px >= 0 && px < cw && py >= 0 && py < ch) {
                    buf[py * cw + px] = col;
                }
            }
        }
    }
}

static void handle_settings_click(Window* w, int x, int y) {
    if (rect_contains(w->x+80, w->y+15+WIN_CAPTION_H+2, 100, 24, x, y)) {
        current_theme_idx = (current_theme_idx + 1) % 2;
    }
    if (rect_contains(w->x+110, w->y+55+WIN_CAPTION_H+2, 80, 24, x, y)) {
        g_wallpaper_enabled = !g_wallpaper_enabled;
        w->state.settings.wallpaper_enabled = g_wallpaper_enabled;
    }
}

static void handle_files_click(Window* w, int x, int y) {
    int cx = w->x + 4;
    int cy = w->y + WIN_CAPTION_H + 4;
    size_t count = fs_file_count();
    for (size_t i = 0; i < count; i++) {
        int ry = cy + 24 + i * 18;
        if (ry + 18 < w->y + w->h && rect_contains(cx + 2, ry, w->w - 12, 18, x, y)) {
            w->state.files.selected_index = (int)i;
            return;
        }
    }
}

static void handle_taskmgr_click(Window* w, int x, int y) {
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    int list_y = cy + 30;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if (windows[i] && windows[i]->visible) {
            if (rect_contains(cx, list_y, w->w - 20, 20, x, y)) {
                w->state.taskmgr.selected_pid = i;
            }
            list_y += 20;
        }
    }
    if (w->state.taskmgr.selected_pid != -1) {
        if (rect_contains(cx + w->w - 80, cy, 60, 24, x, y)) {
             if (windows[w->state.taskmgr.selected_pid] != w) {
                 close_window(w->state.taskmgr.selected_pid);
                 w->state.taskmgr.selected_pid = -1;
             }
        }
    }
}

static void handle_browser_click(Window* w, int x, int y) {
    int cx = w->x;
    int cy = w->y + WIN_CAPTION_H;
    if (rect_contains(cx + w->w - 40, cy + 5, 30, 20, x, y)) {
        str_copy(w->state.browser.status, "Loading...");
    }
}

static void handle_calc_logic(Window* w, char key) {
    (void)key; // Unused for mouse logic
    int cx = w->x + 14; 
    int cy = w->y + WIN_CAPTION_H + 54;
    const char* btns = "789/456*123-C0=+";
    for(int b=0; b<16; b++) {
        int bx = cx + 10 + (b%4)*40; 
        int by = cy + (b/4)*35;
        if (rect_contains(bx, by, 35, 30, mouse.x, mouse.y)) {
            char c = btns[b]; 
            CalcState* s = &w->state.calc;
            if (c >= '0' && c <= '9') {
                int d = c - '0';
                if (s->new_entry) { s->current_val = d; s->new_entry = false; }
                else if (s->current_val < 100000000) s->current_val = s->current_val * 10 + d;
            } 
            else if (c == 'C') { s->current_val = 0; s->accumulator = 0; s->op = 0; s->new_entry = true; } 
            else if (c == '+' || c == '-' || c == '*' || c == '/') { s->accumulator = s->current_val; s->op = c; s->new_entry = true; } 
            else if (c == '=') {
                if (s->op == '+') s->current_val = s->accumulator + s->current_val;
                else if (s->op == '-') s->current_val = s->accumulator - s->current_val;
                else if (s->op == '*') s->current_val = s->accumulator * s->current_val;
                else if (s->op == '/') { if(s->current_val!=0) s->current_val = s->accumulator / s->current_val; }
                s->op = 0; s->new_entry = true;
            }
            return;
        }
    }
}

static void handle_terminal_input(Window* w, char c) {
    TerminalState* ts = &w->state.term;
    if (c == '\n') {
        for (int i=0; i<5; i++) str_copy(ts->history[i], ts->history[i+1]);
        char line[80]; 
        str_copy(line, ts->prompt);
        int p_len = kstrlen_local(line); 
        int i = 0; 
        while(ts->input[i] && p_len < 79) line[p_len++] = ts->input[i++];
        line[p_len] = 0; 
        str_copy(ts->history[5], line);
        
        if (kstrcmp(ts->input, "exit") == 0) close_window(w->id);
        else if (kstrcmp(ts->input, "cls") == 0) for(int k=0; k<6; k++) ts->history[k][0] = 0;
        
        ts->input[0] = 0; 
        ts->input_len = 0;
    } 
    else if (c == '\b') { if (ts->input_len > 0) ts->input[--ts->input_len] = 0; } 
    else if (c >= 32 && c <= 126 && ts->input_len < 60) { ts->input[ts->input_len++] = c; ts->input[ts->input_len] = 0; }
}

static void handle_browser_input(Window* w, char c) {
    BrowserState* bs = &w->state.browser;
    if (c == '\b') {
        if (bs->url_len > 0) bs->url[--bs->url_len] = 0;
    } else if (c == '\n') {
        str_copy(bs->status, "Loaded.");
    } else if (c >= 32 && c <= 126 && bs->url_len < 60) {
        bs->url[bs->url_len++] = c;
        bs->url[bs->url_len] = 0;
    }
}

static void update_sysmon(Window* w) {
    SysMonState* s = &w->state.sysmon;
    s->update_tick++;
    if (s->update_tick % 5 == 0) {
        s->head = (s->head + 1) % SYSMON_HIST;
        int cpu = (fast_rand() % 40) + (fast_rand() % 40);
        int mem = 20 + (fast_rand() % 10);
        s->cpu_hist[s->head] = cpu;
        s->mem_hist[s->head] = mem;
    }
}

// --- Drawing ---

static void graphics_fill_rect_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i = 0; i < h; i++) {
        graphics_fill_rect(x, y+i, w, 1, (i < h/2) ? c1 : c2);
    }
}

static void draw_bevel_box(int x, int y, int w, int h, bool sunk) {
    graphics_fill_rect(x, y, w, h, COL_BTN_FACE);
    uint32_t tl = sunk ? COL_BTN_SHADOW : COL_BTN_HILIGHT;
    uint32_t br = sunk ? COL_BTN_HILIGHT : COL_BTN_SHADOW;
    graphics_fill_rect(x, y, w, 1, tl);
    graphics_fill_rect(x, y, 1, h, tl);
    graphics_fill_rect(x, y+h-1, w, 1, br);
    graphics_fill_rect(x+w-1, y, 1, h, br);
}

static void draw_window_border(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, COL_WIN_BODY);
    graphics_fill_rect(x, y, w, 1, 0xFF808080);
    graphics_fill_rect(x, y, 1, h, 0xFF808080);
    graphics_fill_rect(x, y+h-1, w, 1, 0xFF202020);
    graphics_fill_rect(x+w-1, y, 1, h, 0xFF202020);
}

static void draw_icon_bitmap(int x, int y, const uint8_t bitmap[24][24]) {
    for (int ry=0; ry<24; ry++) {
        for (int rx=0; rx<24; rx++) {
            uint8_t c = bitmap[ry][rx];
            if (c != 0) {
                uint32_t col = 0;
                switch(c) {
                    case 1: col = 0xFF000000; break;
                    case 2: col = 0xFF444444; break;
                    case 3: col = 0xFF888888; break;
                    case 4: col = 0xFFFFFFFF; break;
                    case 5: col = 0xFFFFCC00; break;
                    case 6: col = 0xFF0000AA; break;
                    case 7: col = 0xFF00AA00; break;
                    case 8: col = 0xFFAA0000; break;
                }
                graphics_put_pixel(x+rx, y+ry, col);
            }
        }
    }
}

static void draw_wallpaper(void) {
    for (int y = 0; y < screen_h; y++) {
        int r = 0;
        int g = 20 + (y * 80 / screen_h);
        int b = 60 + (y * 140 / screen_h);
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        graphics_fill_rect(0, y, screen_w, 1, color);
    }
    graphics_fill_rect(0, screen_h - 100, screen_w, 100, 0xFFD2B48C);
    
    rand_state = 999;
    for (int i = 0; i < 15; i++) {
        int cx = fast_rand() % screen_w;
        int ch = 30 + (fast_rand() % 50);
        int cw = 10 + (fast_rand() % 30);
        int cy = screen_h - 100 - ch + 10;
        uint32_t ccol = (fast_rand() % 2) ? 0xFFFF7F50 : 0xFFFF69B4; 
        graphics_fill_rect(cx, cy, cw, ch, ccol);
    }
    
    rand_state = (timer_get_ticks() / 10) + 100;
    for (int i = 0; i < 15; i++) {
        int bx = fast_rand() % screen_w;
        int by = fast_rand() % (screen_h - 100);
        graphics_fill_rect(bx, by, 4, 4, 0x80FFFFFF); 
    }
}

static void render_paint_app(Window* w) {
    int cx = w->x + 6; 
    int cy = w->y + WIN_CAPTION_H + 6;
    int cw = w->w - 12; 
    int ch = w->h - WIN_CAPTION_H - 12;
    
    int tool_h = 40;
    graphics_fill_rect(cx, cy, cw, tool_h, 0xFFE0E0E0);
    
    uint32_t colors[] = {0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFFFFFF};
    for(int i=0; i<8; i++) {
        int px = cx + 5 + (i*30);
        int py = cy + 5;
        graphics_fill_rect(px, py, 25, 25, colors[i]);
        if (w->state.paint.current_color == colors[i]) {
            graphics_fill_rect(px, py+26, 25, 3, 0xFF000000);
        }
    }
    
    int cv_y = cy + tool_h;
    int cv_h = ch - tool_h;
    
    if (w->state.paint.canvas_buffer) {
        uint32_t* buf = w->state.paint.canvas_buffer;
        int buf_w = w->state.paint.width;
        int start_x = cx;
        int start_y = cv_y;
        
        for (int y = 0; y < cv_h; y++) {
            for (int x = 0; x < cw; x++) {
                if (x < buf_w) {
                    uint32_t col = buf[y * buf_w + x];
                    graphics_put_pixel(start_x + x, start_y + y, col);
                }
            }
        }
    } else {
        graphics_fill_rect(cx, cv_y, cw, cv_h, COL_WHITE);
    }
}

static void render_window(Window* w) {
    Theme* t = &themes[current_theme_idx];
    if (!w || !w->visible || w->minimized) return;

    if (!w->maximized) graphics_fill_rect_alpha(w->x+6, w->y+6, w->w, w->h, 0x000000, 60);

    if (t->is_glass) {
        graphics_fill_rect_alpha(w->x-2, w->y-2, w->w+4, w->h+4, t->win_border, 100);
        graphics_fill_rect(w->x, w->y, w->w, w->h, t->win_body);
    } else {
        draw_window_border(w->x, w->y, w->w, w->h);
    }

    uint32_t tc = w->focused ? t->win_title_active : t->win_title_inactive;
    if (t->is_glass) {
        graphics_fill_rect_gradient(w->x, w->y, w->w, WIN_CAPTION_H, tc, tc + 0x00202020);
    } else {
        graphics_fill_rect(w->x+2, w->y+2, w->w-4, WIN_CAPTION_H-2, tc);
    }
    graphics_draw_string_scaled(w->x+8, w->y+6, w->title, COL_WHITE, tc, 1);

    int bx = w->x + w->w - 24;
    draw_bevel_box(bx, w->y+4, 18, 18, false);
    graphics_draw_char(bx+5, w->y+9, 'X', COL_BLACK, COL_BTN_FACE);

    int mx = bx - 22;
    draw_bevel_box(mx, w->y+4, 18, 18, false);
    graphics_draw_char(mx+5, w->y+9, '#', COL_BLACK, COL_BTN_FACE);

    int mn = mx - 22;
    draw_bevel_box(mn, w->y+4, 18, 18, false);
    graphics_draw_char(mn+5, w->y+9, '_', COL_BLACK, COL_BTN_FACE);

    int cx = w->x+2; int cy = w->y+WIN_CAPTION_H+2;
    int cw = w->w-4; int ch = w->h-WIN_CAPTION_H-4;
    graphics_fill_rect(cx, cy, cw, ch, COL_WIN_BODY);

    if (w->type == APP_NOTEPAD) {
        draw_bevel_box(cx+2, cy+2, cw-4, ch-4, true);
        graphics_fill_rect(cx+4, cy+4, cw-8, ch-8, COL_WHITE);
        graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
        if ((timer_get_ticks() / 15) % 2) {
             graphics_fill_rect(cx+6+(w->state.notepad.length*8), cy+6, 2, 10, COL_BLACK);
        }
    } 
    else if (w->type == APP_PAINT) {
        render_paint_app(w);
    }
    else if (w->type == APP_TICTACTOE) {
        TicTacToeState* s = &w->state.ttt;
        int gx = cx + 8; int gy = cy + 8;
        // Draw grid
        for (int i=1;i<3;i++) graphics_fill_rect(gx + i*60, gy, 4, 180, COL_BLACK);
        for (int i=1;i<3;i++) graphics_fill_rect(gx, gy + i*60, 180, 4, COL_BLACK);
        
        for (int r=0;r<3;r++) for(int c=0;c<3;c++) {
            int cell_x = gx + c*60 + 20;
            int cell_y = gy + r*60 + 15;
            if (s->board[r][c] == 1) graphics_draw_string_scaled(cell_x, cell_y, "X", 0xFF0000AA, COL_WIN_BODY, 4);
            else if (s->board[r][c] == 2) graphics_draw_string_scaled(cell_x, cell_y, "O", 0xFF00AA00, COL_WIN_BODY, 4);
        }
        
        if (s->winner != 0) {
            const char* msg = (s->winner==1)?"X Wins!":(s->winner==2)?"O Wins!":"Draw!";
            graphics_draw_string_scaled(gx, gy+185, msg, 0xFFFF0000, COL_WIN_BODY, 2);
            draw_bevel_box(gx+10, gy+205, 100, 24, false);
            graphics_draw_string_scaled(gx+25, gy+210, "Restart", COL_BLACK, COL_BTN_FACE, 1);
        } else {
            graphics_draw_string_scaled(gx, gy+185, (s->turn==1)?"Turn: X":"Turn: O", COL_BLACK, COL_WIN_BODY, 1);
        }
    }
    else if (w->type == APP_IMAGEVIEW) {
        int ix = cx + 8; int iy = cy + 8;
        int iw = cw - 16; int ih = ch - 48;
        
        // Procedural Image
        rand_state = w->state.img.seed;
        for (int y=0; y<ih; y+=w->state.img.zoom) {
            for (int x=0; x<iw; x+=w->state.img.zoom) {
                int r = (x ^ y) & 0xFF;
                int g = (x * y) & 0xFF;
                int b = ((x+y)*2) & 0xFF;
                uint32_t col = 0xFF000000 | (r<<16) | (g<<8) | b;
                graphics_fill_rect(ix+x, iy+y, w->state.img.zoom, w->state.img.zoom, col);
            }
        }
        
        draw_bevel_box(ix, iy + ih + 10, 80, 20, false);
        graphics_draw_string_scaled(ix+10, iy+ih+14, "Next Img", COL_BLACK, COL_BTN_FACE, 1);
    }
    else if (w->type == APP_BROWSER) {
        draw_bevel_box(cx+2, cy+2, cw-40, 24, true);
        graphics_fill_rect(cx+4, cy+4, cw-44, 20, COL_WHITE);
        graphics_draw_string_scaled(cx+6, cy+8, w->state.browser.url, COL_BLACK, COL_WHITE, 1);
        draw_bevel_box(cx+cw-35, cy+2, 30, 24, false);
        graphics_draw_string_scaled(cx+cw-28, cy+8, "GO", COL_BLACK, COL_BTN_FACE, 1);
        
        int content_y = cy + 30;
        int content_h = ch - 32;
        graphics_fill_rect(cx+2, content_y, cw-4, content_h, COL_WHITE);
        graphics_draw_string_scaled(cx+10, content_y+10, "Nostalux Web Browser v1.0", 0xFF0000AA, COL_WHITE, 2);
        graphics_draw_string_scaled(cx+10, content_y+40, "Status:", 0xFF555555, COL_WHITE, 1);
        graphics_draw_string_scaled(cx+70, content_y+40, w->state.browser.status, 0xFF00AA00, COL_WHITE, 1);
        graphics_draw_string_scaled(cx+10, content_y+70, "Welcome to the future of browsing!", COL_BLACK, COL_WHITE, 1);
    }
    else if (w->type == APP_TASKMGR) {
        graphics_draw_string_scaled(cx+10, cy+10, "PID  Name        Status", COL_BLACK, COL_WIN_BODY, 1);
        graphics_fill_rect(cx+10, cy+22, cw-20, 1, 0xFF888888);
        int list_y = cy + 30;
        for(int i=0; i<MAX_WINDOWS; i++) {
            if (windows[i] && windows[i]->visible) {
                if (w->state.taskmgr.selected_pid == i) {
                    graphics_fill_rect(cx+8, list_y-2, cw-16, 14, 0xFFCCCCFF);
                }
                char buf[64];
                char pid_s[4]; int_to_str(i, pid_s);
                graphics_draw_string_scaled(cx+10, list_y, pid_s, COL_BLACK, COL_WIN_BODY, 1);
                graphics_draw_string_scaled(cx+50, list_y, windows[i]->title, COL_BLACK, COL_WIN_BODY, 1);
                const char* st = windows[i]->minimized ? "Min" : "Vis";
                graphics_draw_string_scaled(cx+200, list_y, st, COL_BLACK, COL_WIN_BODY, 1);
                list_y += 20;
            }
        }
        draw_bevel_box(cx + cw - 80, cy + 10, 60, 24, false);
        graphics_draw_string_scaled(cx+cw-70, cy+16, "End Task", COL_BLACK, COL_BTN_FACE, 1);
    }
    else if (w->type == APP_SETTINGS) {
        graphics_draw_string_scaled(cx+10, cy+10, "Desktop Wallpaper:", COL_BLACK, COL_WIN_BODY, 1);
        bool on = w->state.settings.wallpaper_enabled;
        draw_bevel_box(cx+10, cy+30, 140, 30, on);
        const char* lbl = on ? "Enabled (Coral)" : "Disabled (Blue)";
        graphics_draw_string_scaled(cx+20, cy+40, lbl, COL_BLACK, COL_BTN_FACE, 1);
        graphics_draw_string_scaled(cx+10, cy+80, "System Theme: Ocean", COL_BLACK, COL_WIN_BODY, 1);
    }
    else if (w->type == APP_TERMINAL) {
        draw_bevel_box(cx+2, cy+2, cw-4, ch-4, true);
        graphics_fill_rect(cx+4, cy+4, cw-8, ch-8, COL_BLACK);
        for(int i=0; i<6; i++) 
            graphics_draw_string_scaled(cx+6, cy+6+(i*10), w->state.term.history[i], 0xFF00FF00, COL_BLACK, 1);
        int input_y = cy+66;
        graphics_draw_string_scaled(cx+6, input_y, w->state.term.prompt, 0xFF00FF00, COL_BLACK, 1);
        int pw = kstrlen_local(w->state.term.prompt)*8;
        graphics_draw_string_scaled(cx+6+pw, input_y, w->state.term.input, COL_WHITE, COL_BLACK, 1);
        if ((timer_get_ticks()/15)%2) graphics_fill_rect(cx+6+pw+(w->state.term.input_len*8), input_y, 8, 8, 0xFF00FF00);
    }
    else if (w->type == APP_CALC) {
        char buf[16]; int_to_str(w->state.calc.current_val, buf);
        draw_bevel_box(cx+10, cy+10, cw-20, 24, true);
        graphics_fill_rect(cx+12, cy+12, cw-24, 20, COL_WHITE);
        graphics_draw_string_scaled(cx+cw-14-(kstrlen_local(buf)*8), cy+16, buf, COL_BLACK, COL_WHITE, 1);
        const char* btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
        for(int i=0; i<16; i++) {
            int bx_pos = cx+10 + (i%4)*40; int by_pos = cy+45 + (i/4)*30;
            bool h = rect_contains(bx_pos, by_pos, 35, 25, mouse.x, mouse.y);
            draw_bevel_box(bx_pos, by_pos, 35, 25, h&&mouse.left_button);
            graphics_draw_char(bx_pos+12, by_pos+8, btns[i][0], COL_BLACK, COL_BTN_FACE);
        }
    }
    else if (w->type == APP_FILES) {
        draw_bevel_box(cx+2, cy+2, cw-4, ch-4, true);
        graphics_fill_rect(cx+4, cy+4, cw-8, ch-8, COL_WHITE);
        graphics_fill_rect(cx+4, cy+4, cw-8, 18, 0xFFCCCCCC);
        graphics_draw_string_scaled(cx+8, cy+8, "Name", COL_BLACK, 0xFFCCCCCC, 1);
        size_t count = fs_file_count();
        for (size_t i=0; i<count; i++) {
            const struct fs_file* f = fs_file_at(i); if(!f) continue;
            int ry = cy+24 + i*18;
            bool sel = ((int)i == w->state.files.selected_index);
            if (sel) graphics_fill_rect(cx+4, ry, cw-8, 18, 0xFF000080);
            graphics_draw_string_scaled(cx+20, ry+4, f->name, sel?COL_WHITE:COL_BLACK, sel?0xFF000080:COL_WHITE, 1);
            char sz[16]; int_to_str(f->size, sz);
            graphics_draw_string_scaled(cx+cw-60, ry+4, sz, sel?COL_WHITE:COL_BLACK, sel?0xFF000080:COL_WHITE, 1);
        }
    }

    graphics_fill_rect(w->x + w->w - RESIZE_HANDLE, w->y + w->h - RESIZE_HANDLE, RESIZE_HANDLE, RESIZE_HANDLE, 0xFF888888);
}

static void render_taskbar(void) {
    Theme* t = &themes[current_theme_idx];
    int ty = screen_h - TASKBAR_H;
    
    if (t->is_glass) graphics_fill_rect_alpha(0, ty, screen_w, TASKBAR_H, t->taskbar, 200);
    else graphics_fill_rect(0, ty, screen_w, TASKBAR_H, t->taskbar);
    
    uint32_t sb = start_menu_open ? 0xFF004400 : 0xFF006600;
    graphics_fill_rect(2, ty+2, 60, TASKBAR_H-4, sb);
    graphics_draw_string_scaled(10, ty+12, "START", COL_WHITE, sb, 1);
    
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
            
            if (active) graphics_fill_rect(tx, ty+TASKBAR_H-2, 100, 2, COL_ACCENT);
            tx += 105;
        }
    }
    
    // Show Desktop Button (Far Right)
    int sd_x = screen_w - 20;
    graphics_fill_rect(sd_x, ty+2, 18, TASKBAR_H-4, 0xFF444444);
    
    char time[16]; syscall_get_time(time);
    graphics_draw_string_scaled(screen_w-90, ty+12, time, COL_WHITE, t->taskbar, 1);
}

static void render_desktop(void) {
    if (g_wallpaper_enabled) {
        draw_wallpaper();
    } else {
        graphics_fill_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
    }
    
    struct { int x, y; const char* lbl; const uint8_t (*bmp)[24]; AppType app; } icons[] = {
        {20, 20, "Terminal", ICON_TERM, APP_TERMINAL},
        {20, 90, "Files", ICON_FOLDER, APP_FILES},
        {20, 160, "Paint", ICON_PAINT, APP_PAINT},
        {20, 230, "Browser", ICON_BROWSER, APP_BROWSER},
        {20, 300, "Calc", ICON_CALC, APP_CALC},
        {20, 370, "Task Mgr", ICON_TASKMGR, APP_TASKMGR},
        {20, 440, "Settings", ICON_SET, APP_SETTINGS},
        {100, 20, "Game", ICON_GAME, APP_TICTACTOE},
        {100, 90, "Images", ICON_IMAGE, APP_IMAGEVIEW},
    };
    
    for (int i=0; i<9; i++) {
        bool h = rect_contains(icons[i].x, icons[i].y, 64, 60, mouse.x, mouse.y);
        if (h) graphics_fill_rect(icons[i].x-5, icons[i].y-5, 50, 50, 0x40FFFFFF);
        draw_icon_bitmap(icons[i].x + 8, icons[i].y, icons[i].bmp);
        graphics_draw_string_scaled(icons[i].x+2, icons[i].y+36, icons[i].lbl, COL_BLACK, 0, 1);
        graphics_draw_string_scaled(icons[i].x+1, icons[i].y+35, icons[i].lbl, COL_WHITE, 0, 1);
    }

    for (int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i]) render_window(windows[i]);
    }

    render_taskbar();

    char time_buf[8]; syscall_get_time(time_buf);
    
    char mouse_pos[16];
    int_to_str(mouse.x, mouse_pos);
    int len = kstrlen_local(mouse_pos);
    mouse_pos[len] = ',';
    int_to_str(mouse.y, mouse_pos+len+1);
    graphics_draw_string_scaled(screen_w-150, screen_h-TASKBAR_H+12, mouse_pos, 0xFF888888, themes[current_theme_idx].taskbar, 1);

    if (start_menu_open) {
        int w = 180; int h = 390; int y = screen_h - TASKBAR_H - h;
        graphics_fill_rect_alpha(0, y, w, h, 0xFF1F1F1F, 240);
        graphics_fill_rect(0, y, w, 1, 0xFF404040);
        struct { int y_off; const char* lbl; AppType app; } items[] = {
            {10, "Browser", APP_BROWSER}, {40, "Terminal", APP_TERMINAL},
            {70, "Paint", APP_PAINT}, {100, "Files", APP_FILES},
            {130, "Task Manager", APP_TASKMGR}, {160, "Notepad", APP_NOTEPAD},
            {190, "Calculator", APP_CALC}, {220, "Minesweeper", APP_MINESWEEPER},
            {250, "Tic-Tac-Toe", APP_TICTACTOE}, {280, "Image Viewer", APP_IMAGEVIEW},
            {310, "Sys Monitor", APP_SYSMON}, {340, "Run...", APP_RUN}
        };
        for(int i=0; i<12; i++) {
            int iy = y + items[i].y_off;
            bool hover = rect_contains(0, iy, w, 28, mouse.x, mouse.y);
            if(hover) graphics_fill_rect(0, iy, w, 28, 0xFF404040);
            graphics_draw_string_scaled(20, iy+8, items[i].lbl, COL_WHITE, hover?0xFF404040:0xFF1F1F1F, 1);
        }
    }

    mouse_trail[trail_head].x = mouse.x;
    mouse_trail[trail_head].y = mouse.y;
    trail_head = (trail_head + 1) % TRAIL_LEN;
    for(int i=0; i<TRAIL_LEN; i++) {
        int idx = (trail_head + i) % TRAIL_LEN;
        if (mouse_trail[idx].x != 0)
            graphics_put_pixel(mouse_trail[idx].x, mouse_trail[idx].y, 0xFF00FFFF);
    }

    int mx = mouse.x; int my_pos = mouse.y;
    if (mx < 0) mx = 0; 
    if (mx > screen_w - 12) mx = screen_w - 12;
    if (my_pos < 0) my_pos = 0; 
    if (my_pos > screen_h - 19) my_pos = screen_h - 19;
    
    for(int y=0; y<19; y++) {
        for(int x=0; x<12; x++) {
            if(CURSOR_BITMAP[y][x] == 1) graphics_put_pixel(mx+x, my_pos+y, COL_BLACK);
            else if(CURSOR_BITMAP[y][x] == 2) graphics_put_pixel(mx+x, my_pos+y, COL_WHITE);
        }
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;
    
    if (start_menu_open) {
        int h = 390; int menu_y = ty - h;
        if (x < 180 && y >= menu_y && y < ty) {
            int item_idx = (y - (menu_y + 10)) / 30;
            AppType types[] = {APP_BROWSER, APP_TERMINAL, APP_PAINT, APP_FILES, APP_TASKMGR, APP_NOTEPAD, APP_CALC, APP_MINESWEEPER, APP_TICTACTOE, APP_IMAGEVIEW, APP_SYSMON, APP_RUN};
            if (item_idx >= 0 && item_idx < 12) {
                create_window(types[item_idx], "App", 400, 300);
            }
            start_menu_open = false;
            return;
        }
        start_menu_open = false;
    }

    if (y >= ty) {
        // Show Desktop Button
        if (x > screen_w - 20) {
            g_desktop_shown_mode = !g_desktop_shown_mode;
            for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) {
                windows[i]->minimized = g_desktop_shown_mode;
            }
            return;
        }

        if (x < 70) { start_menu_open = !start_menu_open; return; }
        int tx = 70;
        for(int i=0; i<MAX_WINDOWS; i++) {
            if(windows[i] && windows[i]->visible) {
                if(x >= tx && x < tx+100) {
                    if (windows[i]->focused && !windows[i]->minimized) windows[i]->minimized = true;
                    else { windows[i]->minimized = false; focus_window(i); }
                    return;
                }
                tx += 105;
            }
        }
        return;
    }

    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = windows[i];
        if (w && w->visible && !w->minimized) {
            if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
                int idx = focus_window(i); w = windows[idx];
                
                if (x > w->x + w->w - RESIZE_HANDLE && y > w->y + w->h - RESIZE_HANDLE) {
                    w->resizing = true;
                    w->drag_off_x = x - w->w;
                    w->drag_off_y = y - w->h;
                    return;
                }

                if (y < w->y + WIN_CAPTION_H) {
                    int bx = w->x + w->w - 26;
                    if (rect_contains(bx, w->y+4, 20, 20, x, y)) { close_window(idx); return; }
                    int mx = bx - 22;
                    if (rect_contains(mx, w->y+4, 18, 18, x, y)) { toggle_maximize(w); return; }
                    int mn = mx - 22;
                    if (rect_contains(mn, w->y+4, 18, 18, x, y)) { w->minimized = true; return; }
                    w->dragging = true; w->drag_off_x = x - w->x; w->drag_off_y = y - w->y;
                    return;
                }
                if (w->type == APP_PAINT) handle_paint_click(w, x, y);
                if (w->type == APP_SETTINGS) handle_settings_click(w, x, y);
                if (w->type == APP_FILES) handle_files_click(w, x, y);
                if (w->type == APP_BROWSER) handle_browser_click(w, x, y);
                if (w->type == APP_TASKMGR) handle_taskmgr_click(w, x, y);
                if (w->type == APP_CALC) handle_calc_logic(w, 0);
                if (w->type == APP_MINESWEEPER) {
                    int rel_x = x - (w->x+4); int rel_y = y - (w->y+WIN_CAPTION_H+2);
                    handle_minesweeper(w, rel_x, rel_y, mouse.right_button);
                }
                if (w->type == APP_TICTACTOE) handle_tictactoe(w, x, y);
                if (w->type == APP_IMAGEVIEW) handle_imageview(w, x, y);
                if (w->type == APP_RUN) {
                    if (rect_contains(w->x+w->w-60, w->y+w->h-30, 50, 24, x, y)) handle_run_command(w);
                }
                return;
            }
        }
    }
    
    struct { int x, y; const char* n; AppType t; } icons[] = {
        {20, 20, "Terminal", APP_TERMINAL}, {20, 90, "Files", APP_FILES},
        {20, 160, "Paint", APP_PAINT}, {20, 230, "Browser", APP_BROWSER},
        {20, 300, "Calc", APP_CALC}, {20, 370, "Task Mgr", APP_TASKMGR},
        {20, 440, "Settings", APP_SETTINGS}, {100, 20, "Tic-Tac-Toe", APP_TICTACTOE},
        {100, 90, "ImageView", APP_IMAGEVIEW}
    };
    for(int i=0; i<9; i++) {
        if (rect_contains(icons[i].x, icons[i].y, 60, 60, x, y)) {
            create_window(icons[i].t, icons[i].n, 400, 300); return;
        }
    }
}

void gui_demo_run(void) {
    syscall_log("GUI: Starting Glass Desktop...");
    g_gui_running = true;
    graphics_enable_double_buffer();
    screen_w = graphics_get_width(); screen_h = graphics_get_height();
    mouse.x = screen_w / 2; mouse.y = screen_h / 2;
    for(int i=0; i<MAX_WINDOWS; i++) windows[i] = NULL;
    create_window(APP_WELCOME, "Welcome", 350, 200);

    while(1) {
        syscall_yield();
        char c = keyboard_poll_char();
        if (c == 27) break; 
        Window* top = get_top_window();
        if (c && top && top->focused) {
            if(top->type == APP_TERMINAL) handle_terminal_input(top, c);
            if(top->type == APP_BROWSER) handle_browser_input(top, c);
            if(top->type == APP_RUN) {
                RunState* r = &top->state.run;
                if (c == '\n') handle_run_command(top);
                else if (c == '\b') { if(r->len>0) r->cmd[--r->len]=0; }
                else if (r->len < 30) { r->cmd[r->len++]=c; r->cmd[r->len]=0; }
            }
            if (top->type == APP_NOTEPAD) {
                NotepadState* ns = &top->state.notepad;
                if (c == '\b') { if (ns->length > 0) ns->buffer[--ns->length] = 0; }
                else if (c >= 32 && c <= 126 && ns->length < 510) { ns->buffer[ns->length++] = c; ns->buffer[ns->length] = 0; }
            }
        }

        prev_mouse = mouse; 
        syscall_get_mouse(&mouse);
        
        if (mouse.left_button && top) {
            if (top->dragging) {
                top->x = mouse.x - top->drag_off_x; top->y = mouse.y - top->drag_off_y;
            } else if (top->resizing) {
                int nw = mouse.x - top->drag_off_x;
                int nh = mouse.y - top->drag_off_y;
                if (nw < top->min_w) nw = top->min_w;
                if (nh < top->min_h) nh = top->min_h;
                top->w = nw; top->h = nh;
                if(top->type == APP_PAINT && top->state.paint.canvas_buffer) {
                    top->state.paint.width = top->w - 12;
                }
            } else if (top->type == APP_PAINT && rect_contains(top->x+6, top->y+WIN_CAPTION_H+36, top->w-12, top->h-WIN_CAPTION_H-12, mouse.x, mouse.y)) {
                // Paint dragging logic
                handle_paint_click(top, mouse.x, mouse.y);
            }
        }
        
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        if (!mouse.left_button) for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) {
            windows[i]->dragging = false;
            windows[i]->resizing = false;
        }

        render_desktop();
        graphics_swap_buffer();
    }
    
    for(int i=0; i<MAX_WINDOWS; i++) close_window(i);
    g_gui_running = false;
    syscall_exit();
}
