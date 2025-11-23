#include "background.h"
#include <stddef.h>
#include "os_info.h"
#include "syslog.h"
#include "terminal.h"
#include "kstring.h"

// The row index in draw_title_panel where "Welcome to NostaluxOS" is printed.
// It's the 9th line (index 8).
#define BANNER_ROW 8
#define BANNER_WIDTH 78 // Approx width inside borders

static void draw_title_panel(void) {
    static const char* PANEL_LINES[] = {
        "==============================================================================",
        " _   _           _        _             ____   _____                           ",
        "| \\ | |         | |      | |           / __ \\ / ____|                          ",
        "|  \\| | ___  ___| |_ __ _| |_   ___  _| |  | | (___   " OS_NAME "            ",
        "| . ` |/ _ \\ / __| __/ _` | | | | \\ \\/ / |  | |\\___ \\                         ",
        "| |\\  | (_) \\__ \\ || (_| | | |_| |>  <| |__| |____) |                        ",
        "|_| \\_|\\___/|___/\\__\\__,_|_|\\__,_/_/\\_\\\\____/|_____/                         ",
        "==============================================================================",
        "            Welcome to " OS_NAME "                       ",
        "==============================================================================",
        "",
    };

    terminal_setcolors(0x0E, 0x01); // Yellow on Blue
    for (size_t i = 0; i < sizeof(PANEL_LINES) / sizeof(PANEL_LINES[0]); i++) {
        terminal_writestring(PANEL_LINES[i]);
        terminal_newline();
    }
}

static void draw_grid_panel(void) {
    static const char* GRID_PATTERN = "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~";
    static const uint8_t GRID_COLORS[] = {0x0D, 0x0B, 0x0F};

    for (size_t i = 0; i < sizeof(GRID_COLORS) / sizeof(GRID_COLORS[0]); i++) {
        terminal_setcolors(GRID_COLORS[i], 0x01);
        terminal_writestring(GRID_PATTERN);
        terminal_newline();
    }
}

void background_render(void) {
    uint8_t original_fg = 0;
    uint8_t original_bg = 0;
    terminal_getcolors(&original_fg, &original_bg);

    terminal_begin_batch();
    terminal_setcolors(0x0F, 0x01);
    terminal_clear();

    draw_title_panel();
    draw_grid_panel();

    terminal_setcolors(original_fg, original_bg);
    terminal_end_batch();

    syslog_write("UI: background refreshed");
}

void background_animate(void) {
    static int offset = 0;
    static int direction = 1;
    static int tick = 0;
    
    // Slow down animation
    if (tick++ < 500) return; // Call freq is high (polling loop)
    tick = 0;

    const char* msg = "Welcome to " OS_NAME;
    size_t msg_len = kstrlen(msg);
    size_t max_offset = BANNER_WIDTH - 2 - msg_len; // -2 for margins

    // Clear line (draw just spaces inside)
    // We assume the background is roughly 80 chars wide.
    char buffer[81];
    for (int i = 0; i < 80; i++) buffer[i] = ' ';
    buffer[80] = '\0';
    
    // Construct animated line
    // We want to preserve spaces, then msg, then spaces
    // The panel assumes centered text originally (~12 spaces padding)
    // Let's bounce it between offset 1 and max_offset
    
    for (size_t i = 0; i < msg_len; i++) {
        buffer[1 + offset + i] = msg[i];
    }
    
    // Update direction
    offset += direction;
    if (offset >= (int)max_offset || offset <= 0) {
        direction = -direction;
    }

    // Write directly to the specific row
    // Color: 0x0E (Yellow) on 0x01 (Blue)
    terminal_write_at(BANNER_ROW, 0, buffer, 0x0E, 0x01);
}
