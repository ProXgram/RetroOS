#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "os_info.h"
#include "shell.h"
#include "system.h"
#include "syslog.h"
#include "terminal.h"

struct shell_command {
    const char* name;
    void (*handler)(const char* args);
    const char* description;
};

static void shell_print_banner(void);
static void shell_print_prompt(void);
static void command_help(const char* args);
static void command_about(const char* args);
static void command_clear(const char* args);
static void command_color(const char* args);
static void command_echo(const char* args);
static void command_ls(const char* args);
static void command_cat(const char* args);
static void command_sysinfo(const char* args);
static void command_logs(const char* args);

static void command_history(const char* args);
static void command_palette(const char* args);

static void log_command_invocation(const char* command_name);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about " OS_NAME},
    {"clear", command_clear, "Clear the screen"},
    {"color", command_color, "Update text colors (0-15)"},
    {"ls", command_ls, "List files in the virtual FS"},
    {"cat", command_cat, "Print a file from the virtual FS"},
    {"history", command_history, "Show recent commands"},
    {"palette", command_palette, "Display VGA color codes"},
    {"sysinfo", command_sysinfo, "Display hardware and memory info"},
    {"logs", command_logs, "Show the latest system logs"},
    {"echo", command_echo, "Display text back to you"},
};
#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define INPUT_CAPACITY 128

static void shell_print_banner(void) {
    terminal_writestring(OS_BANNER_LINE "\n");
    terminal_writestring(OS_WELCOME_LINE "\n");
    terminal_writestring("Type 'help' to list available commands.\n");
}

static void shell_print_prompt(void) {
    terminal_writestring(OS_PROMPT_TEXT);
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
    terminal_writestring(OS_ABOUT_SUMMARY "\n");
    terminal_writestring(OS_ABOUT_FOCUS "\n");
    terminal_writestring(OS_ABOUT_FEATURES "\n");
}

static void command_clear(const char* args) {
    (void)args;
    background_render();
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

static void command_ls(const char* args) {
    (void)args;

    size_t count = fs_entry_count();
    if (count == 0) {
        terminal_writestring("The virtual filesystem is empty.\n");
        return;
    }

    terminal_writestring("Virtual filesystem contents:\n");
    for (size_t i = 0; i < count; i++) {
        const struct fs_entry* entry = fs_entry_at(i);
        if (entry == NULL) {
            continue;
        }
        terminal_writestring("  ");
        terminal_writestring(entry->name);
        terminal_writestring(" - ");
        terminal_writestring(entry->description);
        terminal_newline();
    }
}

static bool copy_filename_token(const char* input, char* buffer, size_t buffer_size) {
    size_t index = 0;
    while (input[index] != '\0' && input[index] != ' ' && input[index] != '\t') {
        if (index + 1 >= buffer_size) {
            return false;
        }
        buffer[index] = input[index];
        index++;
    }
    buffer[index] = '\0';
    return index > 0;
}

static void command_cat(const char* args) {
    const char* cursor = kskip_spaces(args);
    if (*cursor == '\0') {
        terminal_writestring("Usage: cat <filename>\n");
        return;
    }

    char filename[32];
    if (!copy_filename_token(cursor, filename, sizeof(filename))) {
        terminal_writestring("Filename is missing or too long.\n");
        return;
    }

    const struct fs_entry* entry = fs_find(filename);
    if (entry == NULL) {
        terminal_writestring("File not found. Use 'ls' to list available files.\n");
        return;
    }

    terminal_writestring(entry->contents);
}

static void command_history(const char* args) {
    size_t count = keyboard_history_length();
    if (count == 0) {
        terminal_writestring("No commands have been run yet.\n");
        return;
    }

    const char* cursor = kskip_spaces(args);
    size_t start_index = 0;

    if (*cursor != '\0') {
        unsigned int limit = 0;
        if (!kparse_uint(&cursor, &limit)) {
            terminal_writestring("Usage: history [count]\n");
            return;
        }

        cursor = kskip_spaces(cursor);
        if (*cursor != '\0' || limit == 0) {
            terminal_writestring("Usage: history [count]\n");
            return;
        }

        if (limit < count) {
            start_index = count - limit;
        }
    }

    terminal_writestring("Recent commands:\n");
    for (size_t i = start_index; i < count; i++) {
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

    uint8_t original_fg = 0;
    uint8_t original_bg = 0;
    terminal_getcolors(&original_fg, &original_bg);

    terminal_writestring("VGA palette codes:\n");
    for (unsigned int i = 0; i < 16; i++) {
        terminal_writestring("  ");
        terminal_write_uint(i);
        if (i < 10) {
            terminal_writestring(" ");
        }
        terminal_writestring(" - ");
        terminal_writestring(COLOR_NAMES[i]);
        terminal_writestring("  ");

        terminal_setcolors((uint8_t)i, (uint8_t)i);
        terminal_writestring("    ");
        terminal_setcolors((uint8_t)i, original_bg);
        terminal_write_char((char)0xDB);
        terminal_write_char((char)0xDB);
        terminal_setcolors(original_fg, original_bg);

        terminal_newline();
    }

    terminal_writestring("Use 'color <fg> <bg>' to apply a favorite combination.\n");
}

static void command_sysinfo(const char* args) {
    (void)args;

    const struct BootInfo* info = system_boot_info();
    const struct system_profile* profile = system_profile_info();

    terminal_writestring("System information:\n");
    terminal_writestring("  OS: " OS_NAME "\n");
    terminal_writestring("  Architecture: ");
    terminal_writestring(profile->architecture);
    terminal_newline();

    terminal_writestring("  Resolution: ");
    terminal_write_uint(info->width);
    terminal_writestring("x");
    terminal_write_uint(info->height);
    terminal_newline();

    terminal_writestring("  Color depth: ");
    terminal_write_uint(info->bpp);
    terminal_writestring("-bit\n");

    terminal_writestring("  Memory total: ");
    terminal_write_uint(profile->memory_total_kb);
    terminal_writestring(" KB\n");

    terminal_writestring("  Memory used: ");
    terminal_write_uint(profile->memory_used_kb);
    if (profile->memory_total_kb > 0) {
        unsigned int percent = (unsigned int)(((uint64_t)profile->memory_used_kb * 100u) /
                                              profile->memory_total_kb);
        terminal_writestring(" KB (");
        terminal_write_uint(percent);
        terminal_writestring("%)\n");
    } else {
        terminal_writestring(" KB\n");
    }

    terminal_writestring("  Files available: ");
    terminal_write_uint((unsigned int)fs_entry_count());
    terminal_newline();

    terminal_writestring("  Log entries: ");
    terminal_write_uint((unsigned int)syslog_length());
    terminal_newline();
}

static void command_logs(const char* args) {
    size_t count = syslog_length();
    if (count == 0) {
        terminal_writestring("No log entries recorded yet.\n");
        return;
    }

    const char* cursor = kskip_spaces(args);
    size_t start_index = 0;

    if (*cursor != '\0') {
        unsigned int limit = 0;
        if (!kparse_uint(&cursor, &limit)) {
            terminal_writestring("Usage: logs [count]\n");
            return;
        }

        cursor = kskip_spaces(cursor);
        if (*cursor != '\0' || limit == 0) {
            terminal_writestring("Usage: logs [count]\n");
            return;
        }

        if (limit < count) {
            start_index = count - limit;
        }
    }

    terminal_writestring("Recent system logs:\n");
    for (size_t i = start_index; i < count; i++) {
        const char* entry = syslog_entry(i);
        if (entry == NULL) {
            continue;
        }
        terminal_write_uint(i + 1);
        terminal_writestring(". ");
        terminal_writestring(entry);
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

static void log_command_invocation(const char* command_name) {
    if (command_name == NULL) {
        return;
    }

    static const char PREFIX[] = "Command: ";
    char buffer[64];
    size_t index = 0;

    for (size_t i = 0; i < sizeof(PREFIX) - 1 && index < sizeof(buffer) - 1; i++) {
        buffer[index++] = PREFIX[i];
    }

    for (size_t i = 0; command_name[i] != '\0' && index < sizeof(buffer) - 1; i++) {
        buffer[index++] = command_name[i];
    }

    buffer[index] = '\0';
    syslog_write(buffer);
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
            log_command_invocation(COMMANDS[i].name);
            COMMANDS[i].handler(cursor);
            return;
        }
    }

    syslog_write("Command: unknown");
    terminal_writestring("Unknown command. Type 'help' for a list of commands.\n");
}

void shell_run(void) {
    char input[INPUT_CAPACITY];

    shell_print_banner();

    for (;;) {
        shell_print_prompt();
        keyboard_read_line(input, sizeof(input));
        execute_command(input);
    }
}
