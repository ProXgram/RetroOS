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

static void syscall_log(const char* msg) {
    __asm__ volatile("int $0x80" : : "D"((uint64_t)2), "S"(msg) : "memory");
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
#define WIN_CAPTION_H 26
#define TASKBAR_H 34

// Colors (Nostalux Ocean Theme)
#define COL_DESKTOP     0xFF004488 
#define COL_TASKBAR     0xFF202020
#define COL_WIN_BODY    0xFFE0E0E0
#define COL_WIN_TITLE_1 0xFF003366
#define COL_WIN_TITLE_2 0xFF0055AA
#define COL_WIN_TEXT    0xFF000000
#define COL_BTN_FACE    0xFFDDDDDD
#define COL_BTN_SHADOW  0xFF555555
#define COL_BTN_HILIGHT 0xFFFFFFFF
#define COL_BLACK       0xFF000000
#define COL_WHITE       0xFFFFFFFF

typedef enum { 
    APP_NONE, 
    APP_WELCOME, 
    APP_NOTEPAD, 
    APP_CALC, 
    APP_FILES, 
    APP_SETTINGS, 
    APP_TERMINAL, 
    APP_BROWSER,
    APP_TASKMGR
} AppType;

typedef struct { 
    int current_val; 
    int accumulator; 
    char op; 
    bool new_entry; 
} CalcState;

typedef struct { 
    char buffer[512]; 
    int length; 
} NotepadState;

typedef struct { 
    int selected_index; 
    int scroll_offset; 
} FileManagerState;

typedef struct { 
    bool wallpaper_enabled;
    int theme_id;
} SettingsState;

typedef struct { 
    char prompt[16]; 
    char input[64]; 
    int input_len; 
    char history[6][64]; 
} TerminalState;

typedef struct {
    char url[64];
    int url_len;
    char status[32];
    int scroll;
} BrowserState;

typedef struct {
    int selected_pid;
} TaskMgrState;

typedef struct {
    int id; 
    AppType type; 
    char title[32]; 
    int x, y, w, h;
    bool visible, minimized, maximized, focused, dragging;
    int drag_off_x, drag_off_y;
    int restore_x, restore_y, restore_w, restore_h;
    union { 
        CalcState calc; 
        NotepadState notepad; 
        FileManagerState files; 
        SettingsState settings; 
        TerminalState term; 
        BrowserState browser;
        TaskMgrState taskmgr;
    } state;
} Window;

static Window* windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;
static bool g_wallpaper_enabled = false; // DISABLED by default for performance

// --- BITMAPS ---

// Arrow Cursor (12x19)
static const uint8_t CURSOR_BITMAP[19][12] = {
    {1,1,0,0,0,0,0,0,0,0,0,0}, {1,2,1,0,0,0,0,0,0,0,0,0}, {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0}, {1,2,2,2,2,1,0,0,0,0,0,0}, {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0}, {1,2,2,2,2,2,2,2,1,0,0,0}, {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0}, {1,2,2,2,2,2,1,1,1,1,1,1}, {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,1,1,2,2,1,0,0,0,0,0}, {1,1,0,1,2,2,1,0,0,0,0,0}, {0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,0,1,2,2,1,0,0,0,0}, {0,0,0,0,0,1,2,2,1,0,0,0}, {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0}
};

// Generic 24x24 Icon Bitmaps
// 0:Trans, 1:Black, 2:DarkGrey, 3:Grey, 4:White, 5:Yellow, 6:Blue, 7:Green, 8:Red

// Terminal Icon
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

// Browser Icon (Globe-ish)
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

// Task Manager Icon (Graph-ish)
static const uint8_t ICON_TASKMGR[24][24] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,7,7,7,7,1,1,1,2,1},
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

// Folder Icon
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

// Notepad Icon
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

// Calc Icon
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

// Settings Icon
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

// Trash Icon
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

// Pseudo-random for wallpaper
static unsigned long rand_state = 1234;
static int fast_rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (unsigned int)(rand_state / 65536) % 32768;
}

static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    syscall_free(windows[index]); 
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
    for (int i = 0; i < MAX_WINDOWS; i++) { 
        if (windows[i] == NULL) { slot = i; break; } 
    }
    
    if (slot == -1) { 
        close_window(0); 
        slot = MAX_WINDOWS - 1; 
        while(slot > 0 && windows[slot] != NULL) slot--;
    }

    Window* win = (Window*)syscall_malloc(sizeof(Window));
    if (!win) return;

    win->id = slot; 
    win->type = type; 
    str_copy(win->title, title);
    win->w = w; 
    win->h = h; 
    
    win->x = 50 + (slot * 30); 
    win->y = 50 + (slot * 30);
    
    if (win->x + w > screen_w) win->x = 40;
    if (win->y + h > screen_h - TASKBAR_H) win->y = 40;

    win->visible = true; 
    win->minimized = false; 
    win->maximized = false; 
    win->dragging = false; 
    win->focused = true;

    // Initialize App State
    if (type == APP_NOTEPAD) { 
        win->state.notepad.length = 0; 
        win->state.notepad.buffer[0] = 0; 
    } 
    else if (type == APP_CALC) { 
        win->state.calc.current_val = 0; 
        win->state.calc.new_entry = true; 
        win->state.calc.accumulator = 0;
    } 
    else if (type == APP_TERMINAL) {
        str_copy(win->state.term.prompt, "user@nostalux $ "); 
        win->state.term.input[0] = 0; 
        win->state.term.input_len = 0;
        for(int k=0; k<6; k++) win->state.term.history[k][0] = 0;
        str_copy(win->state.term.history[0], "NostaluxOS GUI Terminal");
    } 
    else if (type == APP_FILES) { 
        win->state.files.selected_index = -1; 
    }
    else if (type == APP_BROWSER) {
        str_copy(win->state.browser.url, "www.nostalux.org");
        win->state.browser.url_len = kstrlen_local("www.nostalux.org");
        str_copy(win->state.browser.status, "Ready");
    }
    else if (type == APP_TASKMGR) {
        win->state.taskmgr.selected_pid = -1;
    }
    else if (type == APP_SETTINGS) {
        win->state.settings.wallpaper_enabled = g_wallpaper_enabled;
        win->state.settings.theme_id = 0;
    }
    
    windows[slot] = win; 
    focus_window(slot);
}

// --- Interaction Handlers ---

static void handle_settings_click(Window* w, int x, int y) {
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;

    // Toggle Wallpaper Button
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
    // Kill button logic could go here
    int cx = w->x + 10;
    int cy = w->y + WIN_CAPTION_H + 10;
    
    // Simulate list selection
    int list_y = cy + 30;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if (windows[i] && windows[i]->visible) {
            if (rect_contains(cx, list_y, w->w - 20, 20, x, y)) {
                w->state.taskmgr.selected_pid = i;
            }
            list_y += 20;
        }
    }
    
    // Kill button
    if (w->state.taskmgr.selected_pid != -1) {
        if (rect_contains(cx + w->w - 80, cy, 60, 24, x, y)) {
            // Can't kill self properly in this loop without crash risk, but let's try
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
    
    // Go button
    if (rect_contains(cx + w->w - 40, cy + 5, 30, 20, x, y)) {
        str_copy(w->state.browser.status, "Loading...");
        // In a real OS, this would trigger a network request
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

static void handle_calc_logic(Window* w, char key) {
    if (key != 0) return;
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

// --- Drawing Primitives ---

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
    // Modern-ish flat border
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
    // Procedural Underwater Wallpaper (Optimized fill)
    for (int y = 0; y < screen_h; y++) {
        // Deep blue to lighter blue
        int r = 0;
        int g = 20 + (y * 80 / screen_h);
        int b = 60 + (y * 140 / screen_h);
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        graphics_fill_rect(0, y, screen_w, 1, color);
    }

    // Draw Sand at bottom
    graphics_fill_rect(0, screen_h - 100, screen_w, 100, 0xFFD2B48C); // Tan color

    // Draw some "Coral" (Random colored rects)
    rand_state = 999;
    for (int i = 0; i < 15; i++) {
        int cx = fast_rand() % screen_w;
        int ch = 30 + (fast_rand() % 50);
        int cw = 10 + (fast_rand() % 30);
        int cy = screen_h - 100 - ch + 10;
        uint32_t ccol = (fast_rand() % 2) ? 0xFFFF7F50 : 0xFFFF69B4; 
        graphics_fill_rect(cx, cy, cw, ch, ccol);
    }
    
    // Draw Bubbles (lighter load)
    rand_state = (timer_get_ticks() / 10) + 100;
    for (int i = 0; i < 15; i++) {
        int bx = fast_rand() % screen_w;
        int by = fast_rand() % (screen_h - 100);
        graphics_fill_rect(bx, by, 4, 4, 0x80FFFFFF); 
    }
}

// --- Window Rendering ---

static void render_window(Window* w) {
    if (!w || !w->visible || w->minimized) return;

    // Shadow
    if (!w->maximized) graphics_fill_rect(w->x+6, w->y+6, w->w, w->h, 0x40000000);

    draw_window_border(w->x, w->y, w->w, w->h);

    // Title Bar Gradient
    uint32_t tcol1 = w->focused ? COL_WIN_TITLE_1 : 0xFF707070;
    
    graphics_fill_rect(w->x+2, w->y+2, w->w-4, WIN_CAPTION_H, tcol1);
    graphics_draw_string_scaled(w->x+8, w->y+8, w->title, COL_WHITE, tcol1, 1);

    // Controls
    int bx = w->x + w->w - 24;
    draw_bevel_box(bx, w->y+4, 18, 18, false); // Close
    graphics_draw_char(bx+5, w->y+9, 'X', COL_BLACK, COL_BTN_FACE);

    int mx = bx - 22;
    draw_bevel_box(mx, w->y+4, 18, 18, false); // Max
    graphics_draw_char(mx+5, w->y+9, '#', COL_BLACK, COL_BTN_FACE);

    int mn = mx - 22;
    draw_bevel_box(mn, w->y+4, 18, 18, false); // Min
    graphics_draw_char(mn+5, w->y+9, '_', COL_BLACK, COL_BTN_FACE);

    // Client Area
    int cx = w->x+2; int cy = w->y+WIN_CAPTION_H+2;
    int cw = w->w-4; int ch = w->h-WIN_CAPTION_H-4;
    graphics_fill_rect(cx, cy, cw, ch, COL_WIN_BODY);

    // App Specific
    if (w->type == APP_NOTEPAD) {
        draw_bevel_box(cx+2, cy+2, cw-4, ch-4, true);
        graphics_fill_rect(cx+4, cy+4, cw-8, ch-8, COL_WHITE);
        graphics_draw_string_scaled(cx+6, cy+6, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
        // Blinking cursor
        if ((timer_get_ticks() / 15) % 2) {
             graphics_fill_rect(cx+6+(w->state.notepad.length*8), cy+6, 2, 10, COL_BLACK);
        }
    } 
    else if (w->type == APP_BROWSER) {
        // Address Bar
        draw_bevel_box(cx+2, cy+2, cw-40, 24, true);
        graphics_fill_rect(cx+4, cy+4, cw-44, 20, COL_WHITE);
        graphics_draw_string_scaled(cx+6, cy+8, w->state.browser.url, COL_BLACK, COL_WHITE, 1);
        
        // Go Button
        draw_bevel_box(cx+cw-35, cy+2, 30, 24, false);
        graphics_draw_string_scaled(cx+cw-28, cy+8, "GO", COL_BLACK, COL_BTN_FACE, 1);
        
        // Content Area
        int content_y = cy + 30;
        int content_h = ch - 32;
        graphics_fill_rect(cx+2, content_y, cw-4, content_h, COL_WHITE);
        
        // Mock Content
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
                // Highlight selection
                if (w->state.taskmgr.selected_pid == i) {
                    graphics_fill_rect(cx+8, list_y-2, cw-16, 14, 0xFFCCCCFF);
                }
                
                char buf[64];
                // PID
                char pid_s[4]; int_to_str(i, pid_s);
                graphics_draw_string_scaled(cx+10, list_y, pid_s, COL_BLACK, COL_WIN_BODY, 1);
                
                // Name
                graphics_draw_string_scaled(cx+50, list_y, windows[i]->title, COL_BLACK, COL_WIN_BODY, 1);
                
                // Status
                const char* st = windows[i]->minimized ? "Min" : "Vis";
                graphics_draw_string_scaled(cx+200, list_y, st, COL_BLACK, COL_WIN_BODY, 1);
                
                list_y += 20;
            }
        }
        
        // Kill Button at bottom
        draw_bevel_box(cx + cw - 80, cy + 10, 60, 24, false);
        graphics_draw_string_scaled(cx+cw-70, cy+16, "End Task", COL_BLACK, COL_BTN_FACE, 1);
    }
    else if (w->type == APP_SETTINGS) {
        graphics_draw_string_scaled(cx+10, cy+10, "Desktop Wallpaper:", COL_BLACK, COL_WIN_BODY, 1);
        
        // Toggle Button
        bool on = w->state.settings.wallpaper_enabled;
        draw_bevel_box(cx+10, cy+30, 140, 30, on); // Pressed in if on
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
        // Header
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

// --- Desktop Rendering ---

static void render_desktop(void) {
    if (g_wallpaper_enabled) {
        draw_wallpaper();
    } else {
        graphics_fill_rect(0, 0, screen_w, screen_h, COL_DESKTOP);
    }
    
    // Desktop Icons
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
        
        // Text Shadow
        graphics_draw_string_scaled(icons[i].x+2, icons[i].y+36, icons[i].lbl, COL_BLACK, 0, 1);
        graphics_draw_string_scaled(icons[i].x+1, icons[i].y+35, icons[i].lbl, COL_WHITE, 0, 1);
    }

    // Windows (Back to Front)
    for (int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i]) render_window(windows[i]);
    }

    // Taskbar
    int ty = screen_h - TASKBAR_H;
    graphics_fill_rect(0, ty, screen_w, TASKBAR_H, COL_TASKBAR);
    graphics_fill_rect(0, ty, screen_w, 1, 0xFF555555);
    
    // Start Button
    bool start_hover = rect_contains(2, ty+2, 60, TASKBAR_H-4, mouse.x, mouse.y);
    uint32_t s_col = (start_menu_open || (start_hover && mouse.left_button)) ? 0xFF003366 : 0xFF0055AA;
    graphics_fill_rect(2, ty+2, 60, TASKBAR_H-4, s_col);
    graphics_draw_string_scaled(10, ty+10, "Start", COL_WHITE, s_col, 1);

    // Task Buttons
    int tx = 70;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i] && windows[i]->visible) {
            bool active = (windows[i]->focused && !windows[i]->minimized);
            uint32_t bcol = active ? 0xFF444444 : 0xFF333333;
            graphics_fill_rect(tx, ty+2, 100, TASKBAR_H-4, bcol);
            
            char s[12]; int c=0; while(c<10&&windows[i]->title[c]){s[c]=windows[i]->title[c];c++;} s[c]=0;
            graphics_draw_string_scaled(tx+6, ty+10, s, COL_WHITE, bcol, 1);
            
            // Bottom highlight for active
            if(active) graphics_fill_rect(tx, ty+TASKBAR_H-4, 100, 2, 0xFF0088FF);
            tx += 104;
        }
    }

    // System Tray & Mouse Debug
    char time_buf[8]; syscall_get_time(time_buf);
    graphics_draw_string_scaled(screen_w-60, ty+10, time_buf, COL_WHITE, COL_TASKBAR, 1);
    
    // Debug Mouse Coordinates
    char mouse_pos[16];
    int_to_str(mouse.x, mouse_pos);
    int len = kstrlen_local(mouse_pos);
    mouse_pos[len] = ',';
    int_to_str(mouse.y, mouse_pos+len+1);
    graphics_draw_string_scaled(screen_w-150, ty+10, mouse_pos, 0xFF888888, COL_TASKBAR, 1);

    // Start Menu
    if (start_menu_open) {
        int mh = 250; int my = ty - mh;
        draw_window_border(0, my, 160, mh);
        // Sidebar
        graphics_fill_rect(2, my+2, 30, mh-4, 0xFF003366);
        
        struct { int y; const char* lbl; AppType app; } items[] = { 
            {my+10, "Browser", APP_BROWSER}, {my+40, "Terminal", APP_TERMINAL}, 
            {my+70, "Files", APP_FILES}, {my+100, "Task Mgr", APP_TASKMGR},
            {my+130, "Notepad", APP_NOTEPAD}, {my+160, "Calc", APP_CALC}, 
            {my+190, "Settings", APP_SETTINGS}, {my+220, "Shutdown", APP_NONE} 
        };
        for(int i=0; i<8; i++) {
            bool h = rect_contains(34, items[i].y, 120, 24, mouse.x, mouse.y);
            if(h) graphics_fill_rect(34, items[i].y, 120, 24, 0xFFDDDDDD);
            graphics_draw_string_scaled(40, items[i].y+6, items[i].lbl, COL_BLACK, h?0xFFDDDDDD:COL_WIN_BODY, 1);
        }
    }

    // Cursor
    // Ensure mouse is within screen bounds for drawing
    int mx = mouse.x; 
    int my_pos = mouse.y;
    
    if (mx < 0) mx = 0;
    if (mx > screen_w - 12) mx = screen_w - 12;
    if (my_pos < 0) my_pos = 0;
    if (my_pos > screen_h - 19) my_pos = screen_h - 19;

    for(int y=0; y<19; y++) {
        for(int x=0; x<12; x++) {
            if(CURSOR_BITMAP[y][x]) 
                graphics_put_pixel(mx+x, my_pos+y, CURSOR_BITMAP[y][x]==1?COL_BLACK:COL_WHITE);
        }
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;
    
    // Start Menu
    if (start_menu_open) {
        int mh = 250; int my = ty - mh;
        if (x < 160 && y > my) {
            if (rect_contains(34, my+10, 120, 24, x, y)) create_window(APP_BROWSER, "Nostalux Web", 450, 350);
            else if (rect_contains(34, my+40, 120, 24, x, y)) create_window(APP_TERMINAL, "Terminal", 400, 300);
            else if (rect_contains(34, my+70, 120, 24, x, y)) create_window(APP_FILES, "Files", 400, 300);
            else if (rect_contains(34, my+100, 120, 24, x, y)) create_window(APP_TASKMGR, "Task Manager", 300, 300);
            else if (rect_contains(34, my+130, 120, 24, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(34, my+160, 120, 24, x, y)) create_window(APP_CALC, "Calc", 220, 300);
            else if (rect_contains(34, my+190, 120, 24, x, y)) create_window(APP_SETTINGS, "Settings", 250, 200);
            else if (rect_contains(34, my+220, 120, 24, x, y)) syscall_shutdown();
            start_menu_open = false; return;
        }
        start_menu_open = false; 
    }

    // Taskbar
    if (y >= ty) {
        if (rect_contains(0, ty, 65, TASKBAR_H, x, y)) { start_menu_open = !start_menu_open; return; }
        int tx = 70;
        for (int i=0; i<MAX_WINDOWS; i++) {
            if(windows[i] && windows[i]->visible) {
                if (rect_contains(tx, ty, 100, TASKBAR_H, x, y)) {
                    if (windows[i]->focused && !windows[i]->minimized) windows[i]->minimized = true;
                    else {
                        windows[i]->minimized = false;
                        focus_window(i);
                    }
                    return;
                }
                tx += 104;
            }
        }
        return;
    }

    // Windows
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = windows[i];
        if (w && w->visible && !w->minimized) {
            if (rect_contains(w->x, w->y, w->w, w->h, x, y)) {
                int idx = focus_window(i); w = windows[idx];
                
                int bx = w->x + w->w - 24;
                if (rect_contains(bx, w->y+4, 18, 18, x, y)) { close_window(idx); return; }
                
                int mx = bx - 22;
                if (rect_contains(mx, w->y+4, 18, 18, x, y)) { toggle_maximize(w); return; }
                
                int mn = mx - 22;
                if (rect_contains(mn, w->y+4, 18, 18, x, y)) { w->minimized = true; return; }

                if (y < w->y + WIN_CAPTION_H && !w->maximized) {
                    w->dragging = true;
                    w->drag_off_x = x - w->x; w->drag_off_y = y - w->y;
                    return;
                }
                if (w->type == APP_CALC) handle_calc_logic(w, 0);
                if (w->type == APP_SETTINGS) handle_settings_click(w, x, y);
                if (w->type == APP_FILES) handle_files_click(w, x, y);
                if (w->type == APP_BROWSER) handle_browser_click(w, x, y);
                if (w->type == APP_TASKMGR) handle_taskmgr_click(w, x, y);
                return;
            }
        }
    }

    // Desktop Icons
    if (rect_contains(20, 20, 60, 60, x, y)) create_window(APP_TERMINAL, "Terminal", 400, 300);
    else if (rect_contains(20, 90, 60, 60, x, y)) create_window(APP_FILES, "Files", 400, 300);
    else if (rect_contains(20, 160, 60, 60, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 230, 60, 60, x, y)) create_window(APP_BROWSER, "Browser", 450, 350);
    else if (rect_contains(20, 300, 60, 60, x, y)) create_window(APP_CALC, "Calc", 220, 300);
    else if (rect_contains(20, 370, 60, 60, x, y)) create_window(APP_TASKMGR, "Task Mgr", 300, 300);
    else if (rect_contains(20, 440, 60, 60, x, y)) create_window(APP_SETTINGS, "Settings", 250, 200);
}

void gui_demo_run(void) {
    syscall_log("GUI: Started (Nostalux Ocean Theme)");
    g_gui_running = true;
    graphics_enable_double_buffer();
    screen_w = graphics_get_width(); screen_h = graphics_get_height();
    
    // FIX: Initialize mouse position locally to center of screen to prevent startup jump
    mouse.x = screen_w / 2;
    mouse.y = screen_h / 2;
    mouse.left_button = false;
    mouse.right_button = false;
    
    for(int i=0; i<MAX_WINDOWS; i++) windows[i] = NULL;
    create_window(APP_WELCOME, "Welcome", 300, 160);

    while(1) {
        syscall_yield();
        char c = keyboard_poll_char();
        if (c == 27) break; // Escape

        Window* top = get_top_window();
        if (c && top && top->focused && !top->minimized) {
            if (top->type == APP_NOTEPAD) {
                NotepadState* ns = &top->state.notepad;
                if (c=='\b' && ns->length>0) ns->buffer[--ns->length]=0;
                else if (c>=32 && c<=126 && ns->length<510) { ns->buffer[ns->length++]=c; ns->buffer[ns->length]=0; }
            } else if (top->type == APP_TERMINAL) {
                handle_terminal_input(top, c);
            } else if (top->type == APP_BROWSER) {
                handle_browser_input(top, c);
            }
        }

        prev_mouse = mouse; 
        syscall_get_mouse(&mouse);
        
        if (mouse.left_button && top && top->dragging) {
            top->x = mouse.x - top->drag_off_x;
            top->y = mouse.y - top->drag_off_y;
            // Improved dragging constraints
            if (top->y < 0) top->y = 0;
            if (top->y > screen_h - TASKBAR_H - WIN_CAPTION_H) top->y = screen_h - TASKBAR_H - WIN_CAPTION_H;
            if (top->x + top->w < 40) top->x = 40 - top->w;
            if (top->x > screen_w - 40) top->x = screen_w - 40;
        }

        if (mouse.left_button && !prev_mouse.left_button) on_click(mouse.x, mouse.y);
        if (!mouse.left_button && prev_mouse.left_button) 
            for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) windows[i]->dragging = false;

        render_desktop();
        graphics_swap_buffer();
    }
    
    for(int i=0; i<MAX_WINDOWS; i++) if(windows[i]) syscall_free(windows[i]);
    graphics_fill_rect(0, 0, screen_w, screen_h, COL_BLACK);
    graphics_swap_buffer();
    g_gui_running = false;
    syscall_exit();
}
