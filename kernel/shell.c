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

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about RetroOS"},
    {"clear", command_clear, "Clear the screen"},
    {"color", command_color, "Update text colors (0-15)"},
    {"echo", command_echo, "Display text back to you"},
};
#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

static void shell_print_banner(void) {
    terminal_writestring("RetroOS 64-bit demo kernel\n");
    terminal_writestring("Welcome to the RetroOS console!\n");
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
    terminal_writestring("RetroOS is a hobby 64-bit operating system kernel.\n");
    terminal_writestring("It focuses on simplicity and a retro-inspired feel.\n");
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

static void command_echo(const char* args) {
    const char* message = kskip_spaces(args);
    if (*message == '\0') {
        terminal_writestring("Usage: echo <text>\n");
        return;
    }

    terminal_writestring(message);
    terminal_newline();
}

static void execute_command(const char* input) {
    const char* trimmed = kskip_spaces(input);
    if (*trimmed == '\0') {
        return;
    }

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
    char input[128];

    shell_print_banner();

    for (;;) {
        terminal_writestring("\nretro> ");
        keyboard_read_line(input, sizeof(input));
        execute_command(input);
    }
}
