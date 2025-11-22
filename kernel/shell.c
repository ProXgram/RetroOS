#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "memtest.h"
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
static void command_memtest(const char* args);

static void command_history(const char* args);
static void command_palette(const char* args);

static void log_command_invocation(const char* command_name);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about " OS_NAME},
    {"clear", command_clear, "Clear the screen"},
    {"color", command_color, "Update text colors (e.g. 'color 14 1', 'color yellow')"},
    {"ls", command_ls, "List files in the virtual FS"},
    {"cat", command_cat, "Print a file from the virtual FS"},
    {"touch", command_touch, "Create an empty file"},
    {"write", command_write, "Overwrite a file with new text"},
    {"append", command_append, "Append text to a file"},
    {"rm", command_rm, "Remove a file"},
    {"history", command_history, "Show recent commands"},
    {"palette", command_palette, "Display VGA colors or set them (e.g. 'palette cyan')"},
    {"sysinfo", command_sysinfo, "Display hardware and memory info"},
    {"memtest", command_memtest, "Run system memory diagnostics"},
    {"logs", command_logs, "Show the latest system logs"},
    {"echo", command_echo, "Display text back to you"},
};
#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define INPUT_CAPACITY 128

static const char* COLOR_NAMES[16] = {
    "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light Grey",
    "Dark Grey", "Light Blue", "Light Green", "Light Cyan", "Light Red",
    "Light Magenta", "Yellow", "White",
};

// ... Helper functions ...
static int resolve_color_name(const char* input, const char** end_ptr) {
    int best_match = -1;
    size_t best_len = 0;

    for (int i = 0; i < 16; i++) {
        const char* name = COLOR_NAMES[i];
        size_t name_len = kstrlen(name);
        
        const char* s = input;
        const char* p = name;
        bool match = true;
        while (*p) {
            char a = *s;
            char b = *p;
            // simple tolower
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
            s++;
            p++;
        }
        
        if (match) {
            // Ensure full word match or end of string
            if (*s == '\0' || *s == ' ' || *s == '\t') {
                if (name_len > best_len) {
                    best_match = i;
                    best_len = name_len;
                }
            }
        }
    }

    if (best_match != -1) {
        if (end_ptr) *end_ptr = input + best_len;
        return best_match;
    }
    return -1;
}

static bool parse_color_arg(const char** cursor, int* out_color) {
    *cursor = kskip_spaces(*cursor);
    if (**cursor == '\0') return false;

    const char* tmp = *cursor;
    unsigned int val;
    
    // Try number first
    if (kparse_uint(&tmp, &val)) {
        if (val < 16) {
            *out_color = (int)val;
            *cursor = tmp;
            return true;
        }
    }

    // Try name
    int idx = resolve_color_name(*cursor, &tmp);
    if (idx != -1) {
        *out_color = idx;
        *cursor = tmp;
        return true;
    }

    return false;
}

static void apply_color_command(const char* args) {
    const char* cursor = args;
    int fg = -1;
    int bg = -1;

    // Parse first arg (FG)
    if (!parse_color_arg(&cursor, &fg)) {
        terminal_writestring("Usage: color <fg> [bg]\n");
        terminal_writestring("Colors: 0-15 or names (e.g. 'black', 'light blue')\n");
        return;
    }

    // Parse optional second arg (BG)
    parse_color_arg(&cursor, &bg);

    uint8_t current_fg, current_bg;
    terminal_getcolors(&current_fg, &current_bg);

    if (bg == -1) {
        bg = current_bg;
    }

    terminal_setcolors((uint8_t)fg, (uint8_t)bg);
    
    terminal_writestring("Color set to FG: ");
    terminal_writestring(COLOR_NAMES[fg]);
    terminal_writestring(" (");
    terminal_write_uint(fg);
    terminal_writestring("), BG: ");
    terminal_writestring(COLOR_NAMES[bg]);
    terminal_writestring(" (");
    terminal_write_uint(bg);
    terminal_writestring(")\n");
}

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
    apply_color_command(args);
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

    size_t count = fs_file_count();
    if (count == 0) {
        terminal_writestring("No files are available.\n");
        return;
    }

    terminal_writestring("Filesystem contents:\n");
    for (size_t i = 0; i < count; i++) {
        const struct fs_file* entry = fs_file_at(i);
        if (entry == NULL) {
            continue;
        }
        terminal_writestring("  ");
        terminal_writestring(entry->name);
        terminal_writestring(" (");
        terminal_write_uint((unsigned int)entry->size);
        terminal_writestring(" bytes)");
        if (entry->size == 0) {
            terminal_writestring(" [empty]");
        }
        terminal_newline();
    }
}

static bool parse_filename_token(const char* args, char* dest, size_t dest_size, const char** remainder) {
    if (dest == NULL || dest_size == 0) {
        return false;
    }

    const char* start = kskip_spaces(args);
    if (*start == '\0') {
        return false;
    }

    const char* end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }

    size_t length = (size_t)(end - start);
    if (length == 0 || length >= dest_size) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        dest[i] = start[i];
    }
    dest[length] = '\0';

    if (remainder != NULL) {
        *remainder = end;
    }

    return true;
}

static void command_cat(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: cat <filename>\n");
        return;
    }

    const struct fs_file* entry = fs_find(filename);
    if (entry == NULL) {
        terminal_writestring("File not found.\n");
        return;
    }

    if (entry->size == 0) {
        terminal_writestring("<empty file>\n");
        return;
    }

    terminal_writestring(entry->data);
}

static void command_touch(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: touch <filename>\n");
        return;
    }

    if (fs_touch(filename)) {
        terminal_writestring("File ready: ");
        terminal_writestring(filename);
        terminal_newline();
    } else {
        terminal_writestring("Unable to create file (maybe disk is full or name is invalid).\n");
    }
}

static void command_write(const char* args) {
    char filename[FS_MAX_FILENAME];
    const char* remainder = NULL;
    if (!parse_filename_token(args, filename, sizeof(filename), &remainder)) {
        terminal_writestring("Usage: write <filename> <text>\n");
        return;
    }

    const char* text = kskip_spaces(remainder);
    if (*text == '\0') {
        terminal_writestring("Usage: write <filename> <text>\n");
        return;
    }

    if (fs_write(filename, text)) {
        terminal_writestring("Wrote ");
        terminal_write_uint((unsigned int)kstrlen(text));
        terminal_writestring(" bytes to ");
        terminal_writestring(filename);
        terminal_newline();
    } else {
        terminal_writestring("Write failed. Ensure the file name is valid and the text fits.\n");
    }
}

static void command_append(const char* args) {
    char filename[FS_MAX_FILENAME];
    const char* remainder = NULL;
    if (!parse_filename_token(args, filename, sizeof(filename), &remainder)) {
        terminal_writestring("Usage: append <filename> <text>\n");
        return;
    }

    const char* text = kskip_spaces(remainder);
    if (*text == '\0') {
        terminal_writestring("Usage: append <filename> <text>\n");
        return;
    }

    if (fs_append(filename, text)) {
        terminal_writestring("Appended ");
        terminal_write_uint((unsigned int)kstrlen(text));
        terminal_writestring(" bytes to ");
        terminal_writestring(filename);
        terminal_newline();
    } else {
        terminal_writestring("Append failed. Either the file name is invalid or there isn't enough space.\n");
    }
}

static void command_rm(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: rm <filename>\n");
        return;
    }

    if (fs_remove(filename)) {
        terminal_writestring("Removed ");
        terminal_writestring(filename);
        terminal_newline();
    } else {
        terminal_writestring("File not found.\n");
    }
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

static void command_memtest(const char* args) {
    (void)args;
    memtest_run_diagnostic();
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

static void command_palette(const char* args) {
    // If arguments are provided, try to apply them as color settings
    const char* check = kskip_spaces(args);
    if (*check != '\0') {
        apply_color_command(args);
        return;
    }

    uint8_t original_fg = 0;
    uint8_t original_bg = 0;
    terminal_getcolors(&original_fg, &original_bg);

    terminal_begin_batch();
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
    terminal_setcolors(original_fg, original_bg);
    terminal_end_batch();
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
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            
