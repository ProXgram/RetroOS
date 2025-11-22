#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "background.h"
#include "fs.h"
#include "io.h"
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
static void command_foreground(const char* args);
static void command_background(const char* args);
static void command_echo(const char* args);
static void command_ls(const char* args);
static void command_cat(const char* args);
static void command_hexdump(const char* args);
static void command_touch(const char* args);
static void command_write(const char* args);
static void command_append(const char* args);
static void command_rm(const char* args);
static void command_sysinfo(const char* args);
static void command_logs(const char* args);
static void command_memtest(const char* args);
static void command_reboot(const char* args);
static void command_shutdown(const char* args);
static void command_time(const char* args);
static void command_calc(const char* args);

static void command_history(const char* args);
static void command_palette(const char* args);

static void log_command_invocation(const char* command_name);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about " OS_NAME},
    {"clear", command_clear, "Clear the screen"},
    {"time", command_time, "Show current RTC date/time"},
    {"calc", command_calc, "Simple math (e.g. 'calc 10 + 5')"},
    {"foreground", command_foreground, "Set text color"},
    {"background", command_background, "Set background color"},
    {"ls", command_ls, "List files and usage stats"},
    {"cat", command_cat, "Print a file's text content"},
    {"hexdump", command_hexdump, "View file content in hex"},
    {"touch", command_touch, "Create an empty file"},
    {"write", command_write, "Overwrite a file with new text"},
    {"append", command_append, "Append text to a file"},
    {"rm", command_rm, "Remove a file"},
    {"history", command_history, "Show recent commands"},
    {"palette", command_palette, "Display all VGA colors"},
    {"sysinfo", command_sysinfo, "Display hardware info"},
    {"memtest", command_memtest, "Run memory diagnostics"},
    {"logs", command_logs, "Show system logs"},
    {"echo", command_echo, "Display text back to you"},
    {"reboot", command_reboot, "Restart the system"},
    {"shutdown", command_shutdown, "Power off the system"},
};
#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

#define INPUT_CAPACITY 128

static const char* COLOR_NAMES[16] = {
    "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light Grey",
    "Dark Grey", "Light Blue", "Light Green", "Light Cyan", "Light Red",
    "Light Magenta", "Yellow", "White",
};

// --- Helpers ---

static void print_hex(uint64_t value, int nibbles) {
    terminal_writestring("0x");
    for (int shift = (nibbles - 1) * 4; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xF);
        char c = (nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10));
        terminal_write_char(c);
    }
}

// CMOS / RTC Helpers
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, (uint8_t)reg);
    return inb(CMOS_DATA);
}

static void command_time(const char* args) {
    (void)args;
    
    // Wait until RTC update in progress is clear
    while (get_rtc_register(0x0A) & 0x80);

    uint8_t second = get_rtc_register(0x00);
    uint8_t minute = get_rtc_register(0x02);
    uint8_t hour   = get_rtc_register(0x04);
    uint8_t day    = get_rtc_register(0x07);
    uint8_t month  = get_rtc_register(0x08);
    uint8_t year   = get_rtc_register(0x09);
    uint8_t status_b = get_rtc_register(0x0B);

    // Convert BCD to binary if necessary
    if (!(status_b & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour   = ( (hour & 0x0F) + (((hour & 0x70) / 16) * 10) ) | (hour & 0x80);
        day    = (day & 0x0F) + ((day / 16) * 10);
        month  = (month & 0x0F) + ((month / 16) * 10);
        year   = (year & 0x0F) + ((year / 16) * 10);
    }

    // Basic 2000s heuristic
    unsigned int full_year = 2000 + year;

    terminal_writestring("RTC Time (UTC): ");
    
    // YYYY-MM-DD
    terminal_write_uint(full_year);
    terminal_write_char('-');
    if (month < 10) terminal_write_char('0');
    terminal_write_uint(month);
    terminal_write_char('-');
    if (day < 10) terminal_write_char('0');
    terminal_write_uint(day);
    
    terminal_writestring(" ");
    
    // HH:MM:SS
    if (hour < 10) terminal_write_char('0');
    terminal_write_uint(hour);
    terminal_write_char(':');
    if (minute < 10) terminal_write_char('0');
    terminal_write_uint(minute);
    terminal_write_char(':');
    if (second < 10) terminal_write_char('0');
    terminal_write_uint(second);
    
    terminal_newline();
}

// Calc Helper
static void command_calc(const char* args) {
    const char* cursor = kskip_spaces(args);
    unsigned int a = 0, b = 0;
    
    if (!kparse_uint(&cursor, &a)) {
        terminal_writestring("Usage: calc <num> <op> <num>\n");
        return;
    }
    
    cursor = kskip_spaces(cursor);
    char op = *cursor;
    if (op == '\0') {
        terminal_writestring("Usage: calc <num> <op> <num>\n");
        return;
    }
    cursor++; // Skip op
    
    cursor = kskip_spaces(cursor);
    if (!kparse_uint(&cursor, &b)) {
        terminal_writestring("Usage: calc <num> <op> <num>\n");
        return;
    }

    long result = 0;
    bool err = false;

    switch(op) {
        case '+': result = (long)a + (long)b; break;
        case '-': result = (long)a - (long)b; break;
        case '*': result = (long)a * (long)b; break;
        case '/': 
            if (b == 0) {
                terminal_writestring("Error: Division by zero.\n");
                err = true;
            } else {
                result = (long)a / (long)b; 
            }
            break;
        default:
            terminal_writestring("Error: Unknown operator. Use +, -, *, or /.\n");
            err = true;
    }

    if (!err) {
        terminal_writestring("Result: ");
        if (result < 0) {
            terminal_write_char('-');
            terminal_write_uint((unsigned int)(-result));
        } else {
            terminal_write_uint((unsigned int)result);
        }
        terminal_newline();
    }
}

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
    
    if (kparse_uint(&tmp, &val)) {
        if (val < 16) {
            *out_color = (int)val;
            *cursor = tmp;
            return true;
        }
    }

    int idx = resolve_color_name(*cursor, &tmp);
    if (idx != -1) {
        *out_color = idx;
        *cursor = tmp;
        return true;
    }

    return false;
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

// --- Command Handlers ---

static void command_foreground(const char* args) {
    const char* cursor = args;
    int fg = -1;

    if (!parse_color_arg(&cursor, &fg)) {
        terminal_writestring("Usage: foreground <color>\n");
        terminal_writestring("Examples: 'foreground red', 'foreground 14'\n");
        return;
    }

    uint8_t current_fg, current_bg;
    terminal_getcolors(&current_fg, &current_bg);

    if (fg == (int)current_bg) {
         terminal_writestring("Error: Foreground cannot match background.\n");
         return;
    }

    terminal_set_theme((uint8_t)fg, current_bg);
    
    terminal_writestring("Foreground set to: ");
    terminal_writestring(COLOR_NAMES[fg]);
    terminal_newline();
}

static void command_background(const char* args) {
    const char* cursor = args;
    int bg = -1;

    if (!parse_color_arg(&cursor, &bg)) {
        terminal_writestring("Usage: background <color>\n");
        terminal_writestring("Examples: 'background blue', 'background 1'\n");
        return;
    }

    uint8_t current_fg, current_bg;
    terminal_getcolors(&current_fg, &current_bg);

    if ((int)current_fg == bg) {
         terminal_writestring("Error: Background cannot match foreground.\n");
         return;
    }

    terminal_set_theme(current_fg, (uint8_t)bg);

    terminal_writestring("Background set to: ");
    terminal_writestring(COLOR_NAMES[bg]);
    terminal_newline();
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
        
        // Alignment padding
        size_t len = kstrlen(COMMANDS[i].name);
        size_t pad = (len < 12) ? (12 - len) : 1;
        
        while (pad--) terminal_write_char(' ');
        terminal_writestring("- ");
        
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

    terminal_writestring("Filename                        Size\n");
    terminal_writestring("------------------------------  ----------\n");

    size_t total_size = 0;

    for (size_t i = 0; i < count; i++) {
        const struct fs_file* entry = fs_file_at(i);
        if (entry == NULL) {
            continue;
        }
        
        terminal_writestring(entry->name);
        
        size_t name_len = kstrlen(entry->name);
        size_t padding = (name_len < 32) ? (32 - name_len) : 1;
        while (padding--) terminal_write_char(' ');

        terminal_write_uint((unsigned int)entry->size);
        terminal_writestring(" B");
        terminal_newline();

        total_size += entry->size;
    }
    terminal_writestring("------------------------------  ----------\n");
    terminal_writestring("Total: ");
    terminal_write_uint(count);
    terminal_writestring(" files, ");
    terminal_write_uint(total_size);
    terminal_writestring(" bytes used.\n");
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
    terminal_newline();
}

static void command_hexdump(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: hexdump <filename>\n");
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

    const unsigned char* data = (const unsigned char*)entry->data;
    size_t size = entry->size;
    
    for (size_t i = 0; i < size; i += 16) {
        // Print offset
        print_hex(i, 4);
        terminal_writestring(": ");

        // Print Hex
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                print_hex(data[i + j], 2);
                terminal_write_char(' ');
            } else {
                terminal_writestring("   ");
            }
        }

        terminal_writestring("| ");

        // Print ASCII
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = data[i + j];
                if (c >= 32 && c <= 126) {
                    terminal_write_char((char)c);
                } else {
                    terminal_write_char('.');
                }
            }
        }
        terminal_newline();
    }
}

static void command_touch(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: touch <filename>\n");
        return;
    }

    if (fs_touch(filename)) {
        terminal_writestring("File created: ");
        terminal_writestring(filename);
        terminal_newline();
    } else {
        terminal_writestring("Unable to create file (disk full or invalid name).\n");
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
        terminal_writestring("Write failed.\n");
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
        terminal_writestring("Append failed (invalid file or full).\n");
    }
}

static void command_rm(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        terminal_writestring("Usage: rm <filename>\n");
        return;
    }

    if (fs_remove(filename)) {
        terminal_writestring("Deleted ");
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

    terminal_writestring("Display:      ");
    terminal_write_uint(boot->width);
    terminal_writestring("x");
    terminal_write_uint(boot->height);
    terminal_writestring("\n");

    terminal_writestring("Framebuffer:  ");
    print_hex(boot->framebuffer, 16);
    terminal_newline();

    terminal_writestring("Memory:       ");
    terminal_write_uint(profile->memory_used_kb);
    terminal_writestring(" KiB used / ");
    terminal_write_uint(profile->memory_total_kb);
    terminal_writestring(" KiB total\n");

    terminal_writestring("Arch:         ");
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

static void command_reboot(const char* args) {
    (void)args;
    terminal_writestring("Rebooting system...\n");
    
    // 8042 Keyboard Controller Reset
    uint8_t temp = 0x02;
    while (temp & 0x02)
        temp = inb(0x64);
    outb(0x64, 0xFE);
    
    // Halt if reboot fails
    for(;;) {
        __asm__ volatile ("hlt");
    }
}

static void command_shutdown(const char* args) {
    (void)args;
    terminal_writestring("Shutting down...\n");

    // QEMU / Bochs shutdown sequence (older and newer ports)
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "d"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "d"((uint16_t)0xB004));
    
    // VirtualBox shutdown
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x3400), "d"((uint16_t)0x4004));

    terminal_writestring("Shutdown failed (ACPI not implemented). Halting CPU.\n");
    for(;;) {
        __asm__ volatile ("cli; hlt");
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
    (void)args; // Arguments ignored, palette is for viewing only.

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
        
        size_t name_len = kstrlen(COLOR_NAMES[i]);
        size_t pad = (name_len < 12) ? (12 - name_len) : 1;
        while(pad--) terminal_write_char(' ');

        terminal_setcolors((uint8_t)i, original_bg);
        terminal_write_char((char)0xDB);
        terminal_write_char((char)0xDB);
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
