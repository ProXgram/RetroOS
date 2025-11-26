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
#define TASKBAR_H 30

// Colors (Classic Theme)
#define COL_DESKTOP     0xFF3A6EA5 
#define COL_TASKBAR     0xFFC0C0C0
#define COL_WIN_BODY    0xFFC0C0C0
#define COL_WIN_TITLE_1 0xFF000080
#define COL_WIN_TITLE_2 0xFF1084D0
#define COL_WIN_TEXT    0xFFFFFFFF
#define COL_WIN_TEXT_IN 0xFFC0C0C0
#define COL_BTN_FACE    0xFFC0C0C0
#define COL_BTN_SHADOW  0xFF404040
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
    APP_TERMINAL 
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
    int dummy; 
} SettingsState;

typedef struct { 
    char prompt[16]; 
    char input[64]; 
    int input_len; 
    char history[6][64]; 
} TerminalState;

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
    } state;
} Window;

static Window* windows[MAX_WINDOWS];
static bool start_menu_open = false;
static int screen_w, screen_h;
static MouseState mouse;
static MouseState prev_mouse;
static uint32_t desktop_color = COL_DESKTOP;

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

static void close_window(int index) {
    if (index < 0 || index >= MAX_WINDOWS || windows[index] == NULL) return;
    syscall_free(windows[index]); 
    windows[index] = NULL;
    
    // Shift windows down to fill gap
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
    
    win->x = 50 + (slot * 25); 
    win->y = 50 + (slot * 25);
    
    if (win->x + w > screen_w) win->x = 40;
    if (win->y + h > screen_h - TASKBAR_H) win->y = 40;

    win->visible = true; 
    win->minimized = false; 
    win->maximized = false; 
    win->dragging = false; 
    win->focused = true;

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
        str_copy(win->state.term.prompt, "user@ring3 $ "); 
        win->state.term.input[0] = 0; 
        win->state.term.input_len = 0;
        for(int k=0; k<6; k++) win->state.term.history[k][0] = 0;
        str_copy(win->state.term.history[0], "NostaluxOS GUI Terminal");
    } 
    else if (type == APP_FILES) { 
        win->state.files.selected_index = -1; 
    }
    
    windows[slot] = win; 
    focus_window(slot);
}

// --- Interaction Handlers ---

static void handle_settings_click(Window* w, int x, int y) {
    int cx = w->x + 4; 
    int cy = w->y + WIN_CAPTION_H + 4;
    uint32_t colors[] = { 0xFF3A6EA5, 0xFF008080, 0xFF502020, 0xFF000000 };
    
    for (int i = 0; i < 4; i++) {
        int bx = cx + 10 + (i * 40);
        if (rect_contains(bx, cy + 30, 30, 30, x, y)) {
            desktop_color = colors[i];
            return;
        }
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
    // Inner bevel for softer look
    if (!sunk) {
        graphics_fill_rect(x+1, y+1, w-2, 1, COL_BTN_FACE); // Top inner
        graphics_fill_rect(x+1, y+1, 1, h-2, COL_BTN_FACE); // Left inner
        graphics_fill_rect(x+1, y+h-2, w-2, 1, 0xFF808080); // Bot inner
        graphics_fill_rect(x+w-2, y+1, 1, h-2, 0xFF808080); // Right inner
    }
}

static void draw_window_border(int x, int y, int w, int h) {
    // 3D Frame
    graphics_fill_rect(x, y, w, h, COL_BTN_FACE);
    graphics_fill_rect(x, y, w, 1, COL_BTN_HILIGHT);
    graphics_fill_rect(x, y, 1, h, COL_BTN_HILIGHT);
    graphics_fill_rect(x, y+h-1, w, 1, COL_BLACK);
    graphics_fill_rect(x+w-1, y, 1, h, COL_BLACK);
    
    graphics_fill_rect(x+1, y+1, w-2, 1, COL_BTN_FACE);
    graphics_fill_rect(x+1, y+1, 1, h-2, COL_BTN_FACE);
    graphics_fill_rect(x+1, y+h-2, w-2, 1, COL_BTN_SHADOW);
    graphics_fill_rect(x+w-2, y+1, 1, h-2, COL_BTN_SHADOW);
}

// Draws the 24x24 icons
static void draw_icon_bitmap(int x, int y, const uint8_t bitmap[24][24]) {
    for (int ry=0; ry<24; ry++) {
        for (int rx=0; rx<24; rx++) {
            uint8_t c = bitmap[ry][rx];
            if (c != 0) {
                uint32_t col = 0;
                switch(c) {
                    case 1: col = 0xFF000000; break; // Black
                    case 2: col = 0xFF444444; break; // DkGrey
                    case 3: col = 0xFF888888; break; // Grey
                    case 4: col = 0xFFFFFFFF; break; // White
                    case 5: col = 0xFFFFCC00; break; // Yellow
                    case 6: col = 0xFF0000AA; break; // Blue
                    case 7: col = 0xFF00AA00; break; // Green
                    case 8: col = 0xFFAA0000; break; // Red
                }
                graphics_put_pixel(x+rx, y+ry, col);
            }
        }
    }
}

// --- Window Rendering ---

static void render_window(Window* w) {
    if (!w || !w->visible || w->minimized) return;

    // Window shadow (if not maximized)
    if (!w->maximized) {
        graphics_fill_rect(w->x+4, w->y+4, w->w, w->h, 0x50000000);
    }

    // Border
    draw_window_border(w->x, w->y, w->w, w->h);

    // Title Bar
    uint32_t tcol1 = w->focused ? COL_WIN_TITLE_1 : 0xFF707070;
    uint32_t tcol2 = w->focused ? COL_WIN_TITLE_2 : 0xFF909090;
    
    // Gradient Title
    for(int i=0; i<WIN_CAPTION_H-4; i++) {
        graphics_fill_rect(w->x+3, w->y+3+i, w->w-6, 1, tcol1); // Simplified for now
    }
    graphics_fill_rect(w->x+3, w->y+3, w->w-6, WIN_CAPTION_H-4, tcol1);

    graphics_draw_string_scaled(w->x+6, w->y+6, w->title, COL_WIN_TEXT, tcol1, 1);

    // Controls
    int bx = w->x + w->w - 22;
    draw_bevel_box(bx, w->y+5, 16, 14, false); // Close
    graphics_draw_char(bx+4, w->y+8, 'X', COL_BLACK, COL_BTN_FACE);

    int mx = bx - 20;
    draw_bevel_box(mx, w->y+5, 16, 14, false); // Max/Restore
    if (w->maximized) graphics_draw_char(mx+4, w->y+8, '2', COL_BLACK, COL_BTN_FACE); // Placeholder for icon
    else graphics_draw_char(mx+4, w->y+8, '1', COL_BLACK, COL_BTN_FACE);

    int mn = mx - 20;
    draw_bevel_box(mn, w->y+5, 16, 14, false); // Min
    graphics_draw_char(mn+4, w->y+6, '_', COL_BLACK, COL_BTN_FACE);

    // Client Area
    int cx = w->x+4; int cy = w->y+WIN_CAPTION_H+2;
    int cw = w->w-8; int ch = w->h-WIN_CAPTION_H-6;
    graphics_fill_rect(cx, cy, cw, ch, COL_WIN_BODY);

    if (w->type == APP_NOTEPAD) {
        draw_bevel_box(cx, cy, cw, ch, true);
        graphics_fill_rect(cx+2, cy+2, cw-4, ch-4, COL_WHITE);
        graphics_draw_string_scaled(cx+4, cy+4, w->state.notepad.buffer, COL_BLACK, COL_WHITE, 1);
        // Cursor
        if ((timer_get_ticks() / 15) % 2) {
             graphics_fill_rect(cx+4+(w->state.notepad.length*8), cy+4, 2, 10, COL_BLACK);
        }
    } 
    else if (w->type == APP_TERMINAL) {
        draw_bevel_box(cx, cy, cw, ch, true);
        graphics_fill_rect(cx+2, cy+2, cw-4, ch-4, COL_BLACK);
        for(int i=0; i<6; i++) 
            graphics_draw_string_scaled(cx+4, cy+4+(i*10), w->state.term.history[i], 0xFF00FF00, COL_BLACK, 1);
        
        int input_y = cy+64;
        graphics_draw_string_scaled(cx+4, input_y, w->state.term.prompt, 0xFF00FF00, COL_BLACK, 1);
        int pw = kstrlen_local(w->state.term.prompt)*8;
        graphics_draw_string_scaled(cx+4+pw, input_y, w->state.term.input, COL_WHITE, COL_BLACK, 1);
        if ((timer_get_ticks()/15)%2) graphics_fill_rect(cx+4+pw+(w->state.term.input_len*8), input_y, 8, 8, 0xFF00FF00);
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
        draw_bevel_box(cx, cy, cw, ch, true);
        graphics_fill_rect(cx+2, cy+2, cw-4, ch-4, COL_WHITE);
        graphics_fill_rect(cx+2, cy+2, cw-4, 18, COL_BTN_FACE);
        graphics_draw_string_scaled(cx+6, cy+6, "Name", COL_BLACK, COL_BTN_FACE, 1);
        size_t count = fs_file_count();
        for (size_t i=0; i<count; i++) {
            const struct fs_file* f = fs_file_at(i); if(!f) continue;
            int ry = cy+22 + i*16;
            bool sel = ((int)i == w->state.files.selected_index);
            if (sel) graphics_fill_rect(cx+2, ry, cw-4, 16, 0xFF000080);
            draw_icon_bitmap(cx+4, ry, ICON_FOLDER); // Reuse folder icon scaled down? No, too big. 
            // Just draw text for now
            graphics_draw_string_scaled(cx+20, ry+4, f->name, sel?COL_WHITE:COL_BLACK, sel?0xFF000080:COL_WHITE, 1);
            char sz[16]; int_to_str(f->size, sz);
            graphics_draw_string_scaled(cx+cw-60, ry+4, sz, sel?COL_WHITE:COL_BLACK, sel?0xFF000080:COL_WHITE, 1);
        }
    }
    else if (w->type == APP_SETTINGS) {
        graphics_draw_string_scaled(cx+10, cy+10, "Background:", COL_BLACK, COL_WIN_BODY, 1);
        uint32_t colors[] = { 0xFF3A6EA5, 0xFF008080, 0xFF502020, 0xFF000000 };
        for(int i=0; i<4; i++) {
            int bx = cx+10 + i*40;
            draw_bevel_box(bx, cy+28, 30, 30, true);
            graphics_fill_rect(bx+2, cy+30, 26, 26, colors[i]);
        }
    }
    else {
        graphics_draw_string_scaled(cx+20, cy+20, "Welcome to NostaluxOS", COL_BLACK, COL_WIN_BODY, 1);
        graphics_draw_string_scaled(cx+20, cy+40, "Ring 3 GUI Demo", 0xFF555555, COL_WIN_BODY, 1);
    }
}

// --- Desktop Rendering ---

static void render_desktop(void) {
    // Background
    graphics_fill_rect(0, 0, screen_w, screen_h, desktop_color);
    
    // Icons
    struct { int x, y; const char* lbl; const uint8_t (*bmp)[24]; AppType app; } icons[] = {
        {20, 20, "Terminal", ICON_TERM, APP_TERMINAL},
        {20, 90, "My Files", ICON_FOLDER, APP_FILES},
        {20, 160, "Notepad", ICON_NOTE, APP_NOTEPAD},
        {20, 230, "Calc", ICON_CALC, APP_CALC},
        {20, 300, "Settings", ICON_SET, APP_SETTINGS},
        {20, 370, "Trash", ICON_TRASH, APP_NONE}
    };
    
    for (int i=0; i<6; i++) {
        bool h = rect_contains(icons[i].x, icons[i].y, 64, 60, mouse.x, mouse.y);
        if (h) graphics_fill_rect(icons[i].x-5, icons[i].y-5, 50, 50, 0x40000080);
        
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
    draw_bevel_box(0, ty, screen_w, TASKBAR_H, false);
    
    // Start Button
    bool start_hover = rect_contains(2, ty+2, 60, TASKBAR_H-4, mouse.x, mouse.y);
    draw_bevel_box(2, ty+2, 60, TASKBAR_H-4, start_menu_open || (start_hover && mouse.left_button));
    graphics_draw_string_scaled(10, ty+8, "Start", COL_BLACK, COL_BTN_FACE, 1);

    // Task Buttons
    int tx = 70;
    for(int i=0; i<MAX_WINDOWS; i++) {
        if(windows[i] && windows[i]->visible) {
            bool active = (windows[i]->focused && !windows[i]->minimized);
            // Dotted pattern for active window button?
            draw_bevel_box(tx, ty+2, 100, TASKBAR_H-4, active);
            
            char s[12]; int c=0; while(c<10&&windows[i]->title[c]){s[c]=windows[i]->title[c];c++;} s[c]=0;
            graphics_draw_string_scaled(tx+6, ty+8, s, COL_BLACK, COL_BTN_FACE, 1);
            tx += 104;
        }
    }

    // System Tray
    draw_bevel_box(screen_w-70, ty+2, 68, TASKBAR_H-4, true);
    char time_buf[8]; syscall_get_time(time_buf);
    graphics_draw_string_scaled(screen_w-60, ty+8, time_buf, COL_BLACK, COL_BTN_FACE, 1);

    // Start Menu
    if (start_menu_open) {
        int mh = 210; int my = ty - mh;
        draw_window_border(0, my, 140, mh);
        // Sidebar
        graphics_fill_rect(2, my+2, 20, mh-4, COL_WIN_TITLE_1);
        graphics_draw_char(8, my+mh-30, 'O', COL_WHITE, COL_WIN_TITLE_1);
        graphics_draw_char(8, my+mh-40, 'S', COL_WHITE, COL_WIN_TITLE_1);
        
        struct { int y; const char* lbl; AppType app; } items[] = { 
            {my+10, "Terminal", APP_TERMINAL}, {my+40, "Files", APP_FILES}, 
            {my+70, "Notepad", APP_NOTEPAD}, {my+100, "Calc", APP_CALC}, 
            {my+130, "Settings", APP_SETTINGS}, {my+170, "Shutdown", APP_NONE} 
        };
        for(int i=0; i<6; i++) {
            if(i==5) graphics_fill_rect(26, items[i].y-8, 110, 1, 0xFF808080);
            bool h = rect_contains(24, items[i].y, 114, 24, mouse.x, mouse.y);
            if(h) graphics_fill_rect(24, items[i].y, 114, 24, COL_WIN_TITLE_1);
            graphics_draw_string_scaled(34, items[i].y+6, items[i].lbl, h?COL_WHITE:COL_BLACK, h?COL_WIN_TITLE_1:COL_BTN_FACE, 1);
        }
    }

    // Cursor
    int mx = mouse.x; int my = mouse.y;
    for(int y=0; y<19; y++) {
        for(int x=0; x<12; x++) {
            if(CURSOR_BITMAP[y][x]) 
                graphics_put_pixel(mx+x, my+y, CURSOR_BITMAP[y][x]==1?COL_BLACK:COL_WHITE);
        }
    }
}

static void on_click(int x, int y) {
    int ty = screen_h - TASKBAR_H;
    
    // Start Menu
    if (start_menu_open) {
        int mh = 210; int my = ty - mh;
        if (x < 140 && y > my) {
            if (rect_contains(24, my+10, 114, 24, x, y)) create_window(APP_TERMINAL, "Terminal", 400, 300);
            else if (rect_contains(24, my+40, 114, 24, x, y)) create_window(APP_FILES, "Files", 400, 300);
            else if (rect_contains(24, my+70, 114, 24, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
            else if (rect_contains(24, my+100, 114, 24, x, y)) create_window(APP_CALC, "Calc", 220, 300);
            else if (rect_contains(24, my+130, 114, 24, x, y)) create_window(APP_SETTINGS, "Settings", 250, 200);
            else if (rect_contains(24, my+170, 114, 30, x, y)) syscall_shutdown();
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
                
                int bx = w->x + w->w - 22;
                if (rect_contains(bx, w->y+5, 16, 14, x, y)) { close_window(idx); return; }
                
                int mx = bx - 20;
                if (rect_contains(mx, w->y+5, 16, 14, x, y)) { toggle_maximize(w); return; }
                
                int mn = mx - 20;
                if (rect_contains(mn, w->y+5, 16, 14, x, y)) { w->minimized = true; return; }

                if (y < w->y + WIN_CAPTION_H && !w->maximized) {
                    w->dragging = true;
                    w->drag_off_x = x - w->x; w->drag_off_y = y - w->y;
                    return;
                }
                if (w->type == APP_CALC) handle_calc_logic(w, 0);
                if (w->type == APP_SETTINGS) handle_settings_click(w, x, y);
                if (w->type == APP_FILES) handle_files_click(w, x, y);
                return;
            }
        }
    }

    // Desktop Icons
    if (rect_contains(20, 20, 60, 60, x, y)) create_window(APP_TERMINAL, "Terminal", 400, 300);
    else if (rect_contains(20, 90, 60, 60, x, y)) create_window(APP_FILES, "Files", 400, 300);
    else if (rect_contains(20, 160, 60, 60, x, y)) create_window(APP_NOTEPAD, "Notepad", 300, 200);
    else if (rect_contains(20, 230, 60, 60, x, y)) create_window(APP_CALC, "Calc", 220, 300);
    else if (rect_contains(20, 300, 60, 60, x, y)) create_window(APP_SETTINGS, "Settings", 250, 200);
}

void gui_demo_run(void) {
    syscall_log("GUI: Started (Classic Theme)");
    g_gui_running = true;
    graphics_enable_double_buffer();
    screen_w = graphics_get_width(); screen_h = graphics_get_height();
    
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
            } else if (top->type == APP_TERMINAL) handle_terminal_input(top, c);
        }

        prev_mouse = mouse; syscall_get_mouse(&mouse);
        
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
