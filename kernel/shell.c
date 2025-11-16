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
static void command_touch(const char* args);
static void command_write(const char* args);
static void command_append(const char* args);
static void command_rm(const char* args);
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
    {"touch", command_touch, "Create an empty file"},
    {"write", command_write, "Overwrite a file with new text"},
    {"append", command_append, "Append text to a file"},
    {"rm", command_rm, "Remove a file"},
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
    terminal_newline();
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

static void command_ls(const char* args) {
    (void)args;

    size_t count = fs_entry_count();
    if (count == 0) {
        terminal_writestring("No files are available.\n");
        return;
    }

    terminal_writestring("Virtual filesystem:\n");
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

static void copy_trimmed_token(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0) {
        return;
    }

    size_t length = kstrlen(src);
    while (length > 0 && (src[length - 1] == ' ' || src[length - 1] == '\t')) {
        length--;
    }

    if (length >= dest_size) {
        length = dest_size - 1;
    }

    for (size_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
    dest[length] = '\0';
}

static void command_cat(const char* args) {
    const char* filename_start = kskip_spaces(args);
    if (*filename_start == '\0') {
        terminal_writestring("Usage: cat <filename>\n");
        return;
    }

    char filename[48];
    copy_trimmed_token(filename, sizeof(filename), filename_start);

    if (filename[0] == '\0') {
        terminal_writestring("Usage: cat <filename>\n");
        return;
    }

    const struct fs_entry* entry = fs_find(filename);
    if (entry == NULL) {
        terminal_writestring("File not found.\n");
        return;
    }

    terminal_writestring(entry->contents);
}

static void command_sysinfo(const char* args) {
    (void)args;

    const struct BootInfo* boot = system_boot_info();
    const struct system_profile* profile = system_profile_info();

    terminal_writestring("Display: ");
    terminal_write_uint(boot->width);
    terminal_writestring("x");
    terminal_write_uint(boot->height);
    terminal_writestring(" pixels, pitch=");
    terminal_write_uint(boot->pitch);
    terminal_writestring(" bytes\n");

    terminal_writestring("Framebuffer @ 0x");
    // Print framebuffer address in hex (16 digits)
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((boot->framebuffer >> shift) & 0xF);
        char c = (nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10));
        terminal_write_char(c);
    }
    terminal_newline();

    terminal_writestring("Memory: ");
    terminal_write_uint(profile->memory_used_kb);
    terminal_writestring(" KiB used / ");
    terminal_write_uint(profile->memory_total_kb);
    terminal_writestring(" KiB total\n");

    terminal_writestring("Architecture: ");
    terminal_writestring(profile->architecture);
    terminal_newline();
}

static void command_logs(const char* args) {
    (void)args;

    size_t count = syslog_length();
    if (count == 0) {
        terminal_writestring("No log entries recorded yet.\n");
        return;
    }

    terminal_writestring("Recent system logs:\n");
    for (size_t i = 0; i < count; i++) {
        const char* entry = syslog_entry(i);
        if (entry == NULL) {
            continue;
        }
        terminal_writestring("  ");
        terminal_writestring(entry);
        terminal_newline();
    }
}

static void log_command_invocation(const char* command_name) {
    static const char prefix[] = "Command: ";
    char buffer[80];
    size_t index = 0;

    for (size_t i = 0; prefix[i] != '\0' && index + 1 < sizeof(buffer); i++) {
        buffer[index++] = prefix[i];
    }

    if (command_name == NULL) {
        command_name = "<null>";
    }

    for (size_t i = 0; command_name[i] != '\0' && index + 1 < sizeof(buffer); i++) {
        buffer[index++] = command_name[i];
    }

    buffer[index] = '\0';
    syslog_write(buffer);
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

        terminal_setcolors((uint8_t)i, original_bg);
        terminal_write_char((char)0xDB);
        terminal_write_char((char)0xDB);
        terminal_setcolors(original_fg, original_bg);

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
