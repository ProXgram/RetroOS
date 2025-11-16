#include <stddef.h>
#include <stdint.h>

#include "keyboard.h"
#include "kstring.h"
#include "shell.h"
#include "terminal.h"

struct shell_command {
    const char* name;
    void (*handler)(const char* args);
    const char* description;
};

static void shell_print_banner(void);
static void command_help(const char* args);
static void command_about(const char* args);
static void command_clear(const char* args);
static void command_color(const char* args);
static void command_echo(const char* args);

static void command_history(const char* args);
static void command_palette(const char* args);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about NostaluxOS"},
    {"clear", command_clear, "Clear the screen"},
    {"color", command_color, "Update text colors (0-15)"},
    {"history", command_history, "Show recent commands"},
    {"palette", command_palette, "Display VGA color codes"},
    {"echo", command_echo, "Display text back to you"},
};
#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define INPUT_CAPACITY 128
#define HISTORY_LIMIT 16

static char COMMAND_HISTORY[HISTORY_LIMIT][INPUT_CAPACITY];
static size_t history_count;

static void shell_print_banner(void) {
    terminal_writestring("NostaluxOS 64-bit demo kernel\n");
    terminal_writestring("Welcome to the NostaluxOS console!\n");
    terminal_writestring("Type 'help' to list available commands.\n");
}

static void command_help(const char* args) {
    (void)args;
    terminal_writestring("Available commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        terminal_writestring("  ");
        terminal_writestring(COMMANDS[i].name);
        terminal_writestring(" - ");
        terminal_writestring(COMMANDS[i].description);
        terminal_newline();
    }
}

static void command_about(const char* args) {
    (void)args;
    terminal_writestring("NostaluxOS is a hobby 64-bit operating system kernel.\n");
    terminal_writestring("It focuses on simplicity and a retro-inspired feel.\n");
    terminal_writestring("Right now it ships with a text console shell and a handful of utilities.\n");
}

static void command_clear(const char* args) {
    (void)args;
    terminal_clear();
    shell_print_banner();
}

static void command_color(const char* args) {
    const char* cursor = args;
    unsigned int fg;
    unsigned int bg;

    if (!kparse_uint(&cursor, &fg) || !kparse_uint(&cursor, &bg) || fg > 15 || bg > 15) {
        terminal_writestring("Usage: color <fg> <bg> (values 0-15)\n");
        return;
    }

    cursor = kskip_spaces(cursor);
    if (*cursor != '\0') {
        terminal_writestring("Usage: color <fg> <bg> (values 0-15)\n");
        return;
    }

    terminal_setcolors((uint8_t)fg, (uint8_t)bg);
    terminal_writestring("Text colors updated to fg=");
    terminal_write_uint(fg);
    terminal_writestring(", bg=");
    terminal_write_uint(bg);
    terminal_newline();
}

static void command_history(const char* args) {
    (void)args;

    size_t count = keyboard_history_length();
    if (count == 0) {
        terminal_writestring("No commands have been run yet.\n");
        return;
    }

    terminal_writestring("Recent commands:\n");
    for (size_t i = 0; i < count; i++) {
        const char* entry = keyboard_history_entry(i);
        if (entry == NULL) {
            continue;
        }
        terminal_write_uint(i + 1);
        terminal_writestring(". ");
        terminal_writestring(entry);
        terminal_newline();
    }
}

static const char* COLOR_NAMES[16] = {
    "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light Grey",
    "Dark Grey", "Light Blue", "Light Green", "Light Cyan", "Light Red",
    "Light Magenta", "Yellow", "White",
};

static void command_palette(const char* args) {
    (void)args;

    terminal_writestring("VGA palette codes:\n");
    for (unsigned int i = 0; i < 16; i++) {
        terminal_write_uint(i);
        terminal_writestring(": ");
        terminal_writestring(COLOR_NAMES[i]);
        terminal_newline();
    }
}

static void command_echo(const char* args) {
    const char* message = kskip_spaces(args);
    if (*message == '\0') {
        terminal_writestring("Usage: echo <text>\n");
        return;
    }

    terminal_writestring(message);
    terminal_newline();
}

static void history_record(const char* input) {
    size_t trimmed_length = kstrlen(input);
    if (trimmed_length == 0) {
        return;
    }

    size_t slot = history_count % HISTORY_LIMIT;
    size_t copy_length = trimmed_length;
    if (copy_length >= INPUT_CAPACITY) {
        copy_length = INPUT_CAPACITY - 1;
    }

    for (size_t i = 0; i < copy_length; i++) {
        COMMAND_HISTORY[slot][i] = input[i];
    }
    COMMAND_HISTORY[slot][copy_length] = '\0';
    history_count++;
}

static void execute_command(const char* input) {
    const char* trimmed = kskip_spaces(input);
    if (*trimmed == '\0') {
        return;
    }

    keyboard_history_record(trimmed);

    const char* cursor = trimmed;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
        cursor++;
    }

    size_t command_length = (size_t)(cursor - trimmed);

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        size_t name_length = kstrlen(COMMANDS[i].name);
        if (command_length == name_length && kstrncmp(trimmed, COMMANDS[i].name, name_length) == 0) {
            COMMANDS[i].handler(cursor);
            return;
        }
    }

    terminal_writestring("Unknown command. Type 'help' for a list of commands.\n");
}

void shell_run(void) {
    char input[INPUT_CAPACITY];

    shell_print_banner();

    for (;;) {
        terminal_writestring("\nostalux> ");
        keyboard_read_line(input, sizeof(input));
        execute_command(input);
    }
}
