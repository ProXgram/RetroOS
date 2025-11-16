#include "background.h"

#include <stddef.h>

#include "os_info.h"
#include "syslog.h"
#include "terminal.h"

static void draw_title_panel(void) {
    static const char* PANEL_LINES[] = {
        "==============================================================================",
        "   _   _             _        _             _            _   _  _____  _____  ",
        "  | \\ | | ___  _ __ | |_ __ _| | ___   __ _| |_ ___  _ _| \\ | |/ ____|/ ____| ",
        "  |  \\| |/ _ \\| '_ \\| __/ _` | |/ _ \\ / _` | __/ _ \\| '__|  \\| | (___ | |     ",
        "  | |\\  | (_) | | | | || (_| | | (_) | (_| | || (_) | |  | |\\  |\\___ \\| |     ",
        "  |_| \\_|\\___/|_| |_|\\__\\__,_|_|\\___/ \\__,_|\\__\\___/|_|  |_| \\_|____/|_|     ",
        "==============================================================================",
        "            Welcome to the neon-lit world of " OS_NAME "                       ",
        "==============================================================================",
        "",
    };

    terminal_setcolors(0x0E, 0x01);
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

    terminal_setcolors(0x0F, 0x01);
    terminal_clear();

    draw_title_panel();
    draw_grid_panel();

    terminal_setcolors(original_fg, original_bg);

    syslog_write("UI: background refreshed");
}
