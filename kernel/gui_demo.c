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

// --- Global State ---
static volatile bool g_gui_running = false;

bool gui_is_running(void) { return g_gui_running; }
void gui_set_running(bool running) { g_gui_running = running; }

// --- Configuration ---
#define MAX_WINDOWS 16
#define WIN_CAPTION_H 28
#define TASKBAR_H 36

// Colors (Glass/Aero Theme)
#define COL_DESKTOP     0xFF004488 
#define COL_TASKBAR     0xFF101010
#define COL_WIN_BODY    0xFFF0F0F0
#define COL_WIN_TITLE   0xFF2D2D2D
#define COL_BTN_FACE    0xFFDDDDDD
#define COL_BTN_SHADOW  0xFF999999
#define COL_BTN_HILIGHT 0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_WHITE       0xFFFFFFFF
#define COL_ACCENT      0xFF0078D7

typedef enum { 
    APP_NONE, APP_WELCOME, APP_NOTEPAD, APP_CALC, APP_FILES, 
    APP_SETTINGS, APP_TERMINAL, APP_BROWSER, APP_TASKMGR, APP_PAINT
} AppType;

typedef struct { 
    int current_val; int accumulator; char op; bool new_entry; 
} CalcState;

typedef struct { 
    char buffer[512]; int length; 
} NotepadState;

typedef struct { 
    int selected_index; int scroll_offset; 
} FileManagerState;

typedef struct { 
    bool wallpaper_enabled; int theme_id;
} SettingsState;

typedef struct { 
    char prompt[16]; char input[64]; int input_len; char history[6][64]; 
} TerminalState;

typedef struct {
    char url[64]; int url_len; char status[32]; int scroll;
} BrowserState;

typedef struct {
    int selected_pid;
} TaskMgrState;

typedef struct {
    uint32_t* canvas_buffer;
    int width; int height;
    uint32_t current_color; int brush_size;
} PaintState;

typedef struct {
    int id; AppType type; char title[32]; 
    int x, y, w, h;
    bool visible, minimized, maximized, focused, dragging;
    int drag_off_x, drag_off_y;
    int restore_x, restore_y, restore_w, restore_h;
    union { 
        CalcState calc; NotepadState notepad; FileManagerState files; 
        SettingsState settings; TerminalState term; BrowserState browser;
        TaskMgrState taskmgr; PaintState paint;
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
static void render_window(Window* w);
static void draw_wallpaper(void);

// --- BITMAPS (Icons) ---

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

static const uint8_t ICON_NOTE[24][24] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,1,1,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,6,6,6,6,6,6,6,6,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,1,1,1,1,1,1,1,1,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,4,4,4,4,4,4,4,4,4,4,4,4,4,1,0,0,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
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

static const uint8_t ICON_TRASH[24][24] = {
    {0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,4,4,3,4,4,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,4,4,3,3,3,4,3,4,4,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,1,4,3,3,4,4,3,3,4,3,3,4,1,0,0,0,0,0,0,0,0}, 
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0}, 
    {0,1,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,1,0,0,0,0,0,0}, 
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0}, 
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0}, 
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,4,4,1,1,1,1,1,1,1,0,0,0,0,0,0,0}, 
    {0,0,1,1,1,1,1,4,1,1,4,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,4,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,4,1,4,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,4,4,4,4,4,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,4,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,4,1,4,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,4,1,1,1,4,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0}
};

// --- Helpers ---

static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void str_copy(char* dest, const char* src) {
    int i = 0; 
    while (src[i] && i < 63) { 
        dest[i] = src[i]; 
        i++; 
    } 
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

static int kstrlen_local(const char* s) { 
    int len = 0; 
    while(s[len]) len++; 
    return len; 
}

static int kstrcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static unsigned long rand_state = 1234;
static int fast_rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (unsigned int)(rand_state / 65536) % 32768;
}

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
        if (windows[i] != NULL && windows[i]->visible) return windows[i]; 
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
    win->w = w; win->h = h;
    win->x = 50 + (slot * 30); win->y = 50 + (slot * 30);
    if (win->x+w > screen_w) win->x = 40;
    if (win->y+h > screen_h - TASKBAR_H) win->y = 40;

    win->visible = true; win->minimized = false; win->maximized = false; 
    win->dragging = false; win->focused = true;

    // Init State
    if (type == APP_PAINT) {
        int cw = w - 12; int ch = h - WIN_CAPTION_H - 12;
        if (cw > 600) cw = 600; 
        if (ch > 400) ch = 400;
        
        win->state.paint.width = cw;
        win->state.paint.height = ch;
        
        uint32_t* buf = (uint32_t*)syscall_malloc(cw * ch * 4);
        if (buf) {
            for(int i=0; i<cw*ch; i++) buf[i] = 0xFFFFFFFF;
        }
        win->state.paint.canvas_buffer = buf;
        win->state.paint.current_color = 0xFF000000;
        win->state.paint.brush_size = 2;
    }
    else if (type == APP_BROWSER) {
        str_copy(win->state.browser.url, "www.nostalux.org");
        win->state.browser.url_len = kstrlen_local("www.nostalux.org");
        str_copy(win->state.browser.status, "Ready");
    }
    else if (type == APP_TERMINAL) {
        str_copy(win->state.term.prompt, "$ ");
        win->state.term.input[0]=0; win->state.term.input_len=0;
        for(int k=0;k<6;k++) win->state.term.history[k][0]=0;
        str_copy(win->state.term.history[0], "Nostalux OS v1.2");
    }
    else if (type == APP_CALC) {
        win->state.calc.current_val = 0; win->state.calc.new_entry=true;
    }
    else if (type == APP_SETTINGS) {
        win->state.settings.wallpaper_enabled = g_wallpaper_enabled;
    }

    windows[slot] = win;
    focus_window(slot);
}

// --- Interaction Logic ---

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
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    if (rect_contains(cx, cy, 140, 30, x, y)) {
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

// --- Rendering ---

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
    
    uint32_t colors[] = {0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF};
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
    if (!w || !w->visible || w->minimized) return;

    if (!w->maximized) graphics_fill_rect_alpha(w->x+6, w->y+6, w->w, w->h, 0x000000, 60);

    draw_window_border(w->x, w->y, w->w, w->h);

    uint32_t tcol1 = w->focused ? COL_WIN_TITLE_1 : 0xFF707070;
    graphics_fill_rect(w->x+2, w->y+2, w->w-4, WIN_CAPTION_H, tcol1);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_WHITE, tcol1, 1);

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
        {20, 160, "Notepad", ICON_NOTE, APP_NOTEPAD},
        {20, 230, "Browser", ICON_BROWSER, APP_BROWSER},
        {20, 300, "Calc", ICON_CALC, APP_CALC},
        {20, 370, "Task Mgr", ICON_TASKMGR, APP_TASKMGR},
        {20, 440, "Settings", ICON_SET, APP_SETTINGS},
        {100, 20, "Trash", ICON_TRASH, APP_NONE}
    };
    
    for (int i=0; i<8; i++) {
        bool h = rect_contains(icons[i].x, icons[i].y, 64, 60, mouse.x, mouse.y);
        if (h) graphics_fill_rect(icons[i].x-5, icons[i].y-5, 50, 50, 0x40FFFFFF);
        draw_icon_bitmap(icons[i].x + 8, icons[i].y, icons[i].bmp);
        graphics_draw_string_scaled(icons[i].x+2, icons[i].y+36, icons[i].lbl, COL_BLACK, 0, 1);
        graphics_draw_string_scaled(icons[i].x+1, icons[i].y+35, icons[i].lbl, COL_WHITE, 0, 1);
    }

    for (int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i]) render_window(windows[i]);
    }

    int ty = screen_h - TASKBAR_H;
    graphics_fill_rect_alpha(0, ty, screen_w, TASKBAR_H, COL_TASKBAR, 200);
    
    bool start_hover = rect_contains(0, ty, 70, TASKBAR_H, mouse.x, mouse.y);
    uint32_t s_col = start_menu_open ? 0xFF0050A0 : (start_hover ? 0xFF303030 : 0xFF202020);
    graphics_fill_rect(0, ty, 70, TASKBAR_H, s_col);
    graphics_draw_string_scaled(12, ty+10, "Start", COL_WHITE, s_col, 1);
    
    int tx = 80;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i] && windows[i]->visible) {
            bool active = (windows[i]->focused && !windows[i]->minimized);
            uint32_t bcol = active ? 0xFF404040 : 0x00000000;
            if (active) graphics_fill_rect(tx, ty+2, 120, TASKBAR_H-4, bcol);
            if (active) graphics_fill_rect(tx, ty+TASKBAR_H-2, 120, 2, COL_ACCENT);
            char buf[12]; int k=0; while(k<10 && windows[i]->title[k]){buf[k]=windows[i]->title[k]; k++;} buf[k]=0;
            graphics_draw_string_scaled(tx+10, ty+12, buf, COL_WHITE, bcol, 1);
            tx += 124;
        }
    }

    char time_buf[8]; syscall_get_time(time_buf);
    graphics_draw_string_scaled(screen_w - 60, ty+12, time_buf, COL_WHITE, COL_TASKBAR, 1);
    
    if (start_menu_open) {
        int w = 180; int h = 300; int y = screen_h - TASKBAR_H - h;
        graphics_fill_rect_alpha(0, y, w, h, 0xFF1F1F1F, 240);
        graphics_fill_rect(0, y, w, 1, 0xFF404040);
        struct { int y_off; const char* lbl; AppType app; } items[] = {
            {10, "Browser", APP_BROWSER}, {40, "Terminal", APP_TERMINAL},
            {70, "Paint", APP_PAINT}, {100, "Files", APP_FILES},
            {130, "Task Manager", APP_TASKMGR}, {160, "Notepad", APP_NOTEPAD},
            {190, "Calculator", APP_CALC}, {220, "Settings", APP_SETTINGS},
            {260, "Shut Down", APP_NONE}
        };
        for(int i=0; i<9; i++) {
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
    if (mx < 0) mx = 0; if (mx > screen_w - 12) mx = screen_w - 12;
    if (my_pos < 0) my_pos = 0; if (my_pos > screen_h - 19) my_pos = screen_h - 19;
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
        int h = 300; int menu_y = ty - h;
        if (x < 180 && y >= menu_y && y < ty) {
            int rel_y = y - menu_y;
            if (rel_y > 260) syscall_shutdown();
            else if (rel_y > 220) create_window(APP_SETTINGS, "Settings", 300, 200);
            else if (rel_y > 190) create_window(APP_CALC, "Calculator", 220, 300);
            else if (rel_y > 160) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rel_y > 130) create_window(APP_TASKMGR, "Task Manager", 300, 300);
            else if (rel_y > 100) create_window(APP_FILES, "Files", 400, 300);
            else if (rel_y > 70) create_window(APP_PAINT, "Paint", 400, 300);
            else if (rel_y > 40) create_window(APP_TERMINAL, "Terminal", 400, 300);
            else create_window(APP_BROWSER, "Nostalux Web", 500, 400);
            start_menu_open = false; return;
        }
        start_menu_open = false;
    }

    if (y >= ty) {
        if (x < 70) { start_menu_open = !start_menu_open; return; }
        int tx = 80;
        for(int i=0; i<MAX_WINDOWS; i++) {
            if(windows[i] && windows[i]->visible) {
                if(x >= tx && x < tx+120) {
                    if (windows[i]->focused && !windows[i]->minimized) windows[i]->minimized = true;
                    else { windows[i]->minimized = false; focus_window(i); }
                    return;
                }
                tx += 124;
            }
        }
        return;
    }

    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = windows[i];
        if (w && w->visible && !w->minimized) {
            if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
                int idx = focus_window(i); w = windows[idx];
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
                return;
            }
        }
    }
    
    struct { int x, y; const char* n; AppType t; } icons[] = {
        {20, 20, "Terminal", APP_TERMINAL}, {20, 90, "Files", APP_FILES},
        {20, 160, "Paint", APP_PAINT}, {20, 230, "Settings", APP_SETTINGS}
    };
    for(int i=0; i<4; i++) {
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
            if (top->type == APP_NOTEPAD) {
                NotepadState* ns = &top->state.notepad;
                if (c == '\b') { if (ns->length > 0) ns->buffer[--ns->length] = 0; }
                else if (c >= 32 && c <= 126 && ns->length < 510) { ns->buffer[ns->length++] = c; ns->buffer[ns->length] = 0; }
            }
        }

        prev_mouse = mouse; 
        syscall_get_mouse(&mouse);
        
        if (mouse.left_button && top && top->dragging) {
            top->x = mouse.x - top->drag_off_x; top->y = mouse.y - top->drag_off_y;
        }
        
        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        if (mouse.left_button && top && top->type == APP_PAINT && rect_contains(top->x, top->y+WIN_CAPTION_H, top->w, top->h-WIN_CAPTION_H, mouse.x, mouse.y)) {
            handle_paint_click(top, mouse.x, mouse.y);
        }
        if (!mouse.left_button) for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) windows[i]->dragging = false;

        render_desktop();
        graphics_swap_buffer();
    }
    
    for(int i=0; i<MAX_WINDOWS; i++) close_window(i);
    g_gui_running = false;
    syscall_exit();
}
