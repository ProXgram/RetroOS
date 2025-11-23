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
#include "timer.h"
#include "snake.h"
#include "sound.h"
#include "kstdio.h" 
#include "ata.h"    
#include "banner.h"

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

static void command_uptime(const char* args);
static void command_sleep(const char* args);

static void command_snake(const char* args);
static void command_beep(const char* args);
static void command_disktest(const char* args);
static void command_banner(const char* args);

static void log_command_invocation(const char* command_name);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about " OS_NAME},
    {"clear", command_clear, "Clear the screen"},
    {"banner", command_banner, "Show moving banner screensaver"},
    {"time", command_time, "Show current RTC date/time"},
    {"uptime", command_uptime, "Show time since boot"},  
    {"sleep", command_sleep, "Pause for N seconds"},     
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
    {"sysinfo", command_sysinfo, "Display hardware info"},
    {"memtest", command_memtest, "Run memory diagnostics"},
    {"logs", command_logs, "Show system logs"},
    {"echo", command_echo, "Display text back to you"},
    {"snake", command_snake, "Play the Snake game"},
    {"beep", command_beep, "Test PC Speaker"},
    {"disktest", command_disktest, "Test ATA Read/Write"},
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

// CMOS / RTC Helpers
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, (uint8_t)reg);
    return inb(CMOS_DATA);
}

static void command_time(const char* args) {
    (void)args;
    
    while (get_rtc_register(0x0A) & 0x80);

    uint8_t second = get_rtc_register(0x00);
    uint8_t minute = get_rtc_register(0x02);
    uint8_t hour   = get_rtc_register(0x04);
    uint8_t day    = get_rtc_register(0x07);
    uint8_t month  = get_rtc_register(0x08);
    uint8_t year   = get_rtc_register(0x09);
    uint8_t status_b = get_rtc_register(0x0B);

    if (!(status_b & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour   = ( (hour & 0x0F) + (((hour & 0x70) / 16) * 10) ) | (hour & 0x80);
        day    = (day & 0x0F) + ((day / 16) * 10);
        month  = (month & 0x0F) + ((month / 16) * 10);
        year   = (year & 0x0F) + ((year / 16) * 10);
    }

    unsigned int full_year = 2000 + year;
    kprintf("RTC Time (UTC): %u-%02u-%02u %02u:%02u:%02u\n", 
            full_year, month, day, hour, minute, second);
}

static void command_uptime(const char* args) {
    (void)args;
    uint64_t seconds = timer_get_uptime();
    kprintf("System Uptime: %u seconds (%u ticks)\n", 
            (unsigned int)seconds, (unsigned int)timer_get_ticks());
}

static void command_sleep(const char* args) {
    const char* ptr = kskip_spaces(args);
    unsigned int sec = 0;
    if (!kparse_uint(&ptr, &sec)) {
        kprintf("Usage: sleep <seconds>\n");
        return;
    }
    
    kprintf("Sleeping for %u seconds...\n", sec);
    timer_wait((int)sec * 100);
    kprintf("Done.\n");
}

static void command_calc(const char* args) {
    const char* cursor = kskip_spaces(args);
    unsigned int a = 0, b = 0;
    
    if (!kparse_uint(&cursor, &a)) {
        kprintf("Usage: calc <num> <op> <num>\n");
        return;
    }
    
    cursor = kskip_spaces(cursor);
    char op = *cursor;
    if (op == '\0') {
        kprintf("Usage: calc <num> <op> <num>\n");
        return;
    }
    cursor++; // Skip op
    
    cursor = kskip_spaces(cursor);
    if (!kparse_uint(&cursor, &b)) {
        kprintf("Usage: calc <num> <op> <num>\n");
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
                kprintf("Error: Division by zero.\n");
                err = true;
            } else {
                result = (long)a / (long)b; 
            }
            break;
        default:
            kprintf("Error: Unknown operator. Use +, -, *, or /.\n");
            err = true;
    }

    if (!err) {
        kprintf("Result: %d\n", (int)result);
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

static void command_foreground(const char* args) {
    const char* cursor = args;
    int fg = -1;

    if (!parse_color_arg(&cursor, &fg)) {
        kprintf("Usage: foreground <color>\n");
        return;
    }

    uint8_t current_fg, current_bg;
    terminal_getcolors(&current_fg, &current_bg);

    if (fg == (int)current_bg) {
         kprintf("Error: Foreground cannot match background.\n");
         return;
    }

    terminal_set_theme((uint8_t)fg, current_bg);
    kprintf("Foreground set to: %s\n", COLOR_NAMES[fg]);
}

static void command_background(const char* args) {
    const char* cursor = args;
    int bg = -1;

    if (!parse_color_arg(&cursor, &bg)) {
        kprintf("Usage: background <color>\n");
        return;
    }

    uint8_t current_fg, current_bg;
    terminal_getcolors(&current_fg, &current_bg);

    if ((int)current_fg == bg) {
         kprintf("Error: Background cannot match foreground.\n");
         return;
    }

    terminal_set_theme(current_fg, (uint8_t)bg);
    kprintf("Background set to: %s\n", COLOR_NAMES[bg]);
}

static void shell_print_banner(void) {
    kprintf("%s\n", OS_BANNER_LINE);
    kprintf("%s\n", OS_WELCOME_LINE);
    kprintf("Type 'help' to list available commands.\n");
}

static void shell_print_prompt(void) {
    terminal_newline();
    kprintf(OS_PROMPT_TEXT);
}

static void command_help(const char* args) {
    (void)args;
    kprintf("Available commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        kprintf("  %s", COMMANDS[i].name);
        
        size_t len = kstrlen(COMMANDS[i].name);
        size_t pad = (len < 12) ? (12 - len) : 1;
        while (pad--) terminal_write_char(' ');
        
        kprintf("- %s\n", COMMANDS[i].description);
    }
    terminal_newline();
    kprintf("Keys:\n");
    kprintf("  PageUp / PageDown : Scroll terminal history\n");
    kprintf("  Up / Down Arrow   : Scroll command history\n");
}

static void command_about(const char* args) {
    (void)args;
    kprintf("%s\n%s\n%s\n", OS_ABOUT_SUMMARY, OS_ABOUT_FOCUS, OS_ABOUT_FEATURES);
}

static void command_clear(const char* args) {
    (void)args;
    background_render();
    shell_print_banner();
}

static void command_history(const char* args) {
    size_t count = keyboard_history_length();
    if (count == 0) {
        kprintf("No commands have been run yet.\n");
        return;
    }

    const char* cursor = kskip_spaces(args);
    size_t start_index = 0;

    if (*cursor != '\0') {
        unsigned int limit = 0;
        if (!kparse_uint(&cursor, &limit)) {
            kprintf("Usage: history [count]\n");
            return;
        }
        if (limit < count) {
            start_index = count - limit;
        }
    }

    kprintf("Recent commands:\n");
    for (size_t i = start_index; i < count; i++) {
        const char* entry = keyboard_history_entry(i);
        if (entry == NULL) continue;
        kprintf("%u. %s\n", (unsigned int)(i + 1), entry);
    }
}

static void command_ls(const char* args) {
    (void)args;
    size_t count = fs_file_count();
    if (count == 0) {
        kprintf("No files are available.\n");
        return;
    }

    kprintf("Filename                        Size\n");
    kprintf("------------------------------  ----------\n");

    size_t total_size = 0;

    for (size_t i = 0; i < count; i++) {
        const struct fs_file* entry = fs_file_at(i);
        if (entry == NULL) continue;
        
        terminal_writestring(entry->name);
        size_t name_len = kstrlen(entry->name);
        size_t padding = (name_len < 32) ? (32 - name_len) : 1;
        while (padding--) terminal_write_char(' ');

        kprintf("%u B\n", (unsigned int)entry->size);
        total_size += entry->size;
    }
    kprintf("------------------------------  ----------\n");
    kprintf("Total: %u files, %u bytes used.\n", count, total_size);
}

static void command_cat(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        kprintf("Usage: cat <filename>\n");
        return;
    }

    const struct fs_file* entry = fs_find(filename);
    if (entry == NULL) {
        kprintf("File not found.\n");
        return;
    }

    if (entry->size == 0) {
        kprintf("<empty file>\n");
        return;
    }

    terminal_writestring(entry->data);
    terminal_newline();
}

static void command_hexdump(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        kprintf("Usage: hexdump <filename>\n");
        return;
    }

    const struct fs_file* entry = fs_find(filename);
    if (entry == NULL) {
        kprintf("File not found.\n");
        return;
    }

    if (entry->size == 0) {
        kprintf("<empty file>\n");
        return;
    }

    const unsigned char* data = (const unsigned char*)entry->data;
    size_t size = entry->size;
    
    for (size_t i = 0; i < size; i += 16) {
        kprintf("0x%x: ", (unsigned int)i);
        
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char b = data[i + j];
                if (b < 0x10) terminal_write_char('0');
                kprintf("%x ", b);
            } else {
                kprintf("   ");
            }
        }
        kprintf("| ");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = data[i + j];
                terminal_write_char((c >= 32 && c <= 126) ? (char)c : '.');
            }
        }
        terminal_newline();
    }
}

static void command_touch(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        kprintf("Usage: touch <filename>\n");
        return;
    }

    if (fs_touch(filename)) {
        kprintf("File created: %s\n", filename);
    } else {
        kprintf("Unable to create file.\n");
    }
}

static void command_write(const char* args) {
    char filename[FS_MAX_FILENAME];
    const char* remainder = NULL;
    if (!parse_filename_token(args, filename, sizeof(filename), &remainder)) {
        kprintf("Usage: write <filename> <text>\n");
        return;
    }

    const char* text = kskip_spaces(remainder);
    if (fs_write(filename, text)) {
        kprintf("Wrote %u bytes to %s\n", (unsigned int)kstrlen(text), filename);
    } else {
        kprintf("Write failed.\n");
    }
}

static void command_append(const char* args) {
    char filename[FS_MAX_FILENAME];
    const char* remainder = NULL;
    if (!parse_filename_token(args, filename, sizeof(filename), &remainder)) {
        kprintf("Usage: append <filename> <text>\n");
        return;
    }

    const char* text = kskip_spaces(remainder);
    if (fs_append(filename, text)) {
        kprintf("Appended %u bytes to %s\n", (unsigned int)kstrlen(text), filename);
    } else {
        kprintf("Append failed.\n");
    }
}

static void command_rm(const char* args) {
    char filename[FS_MAX_FILENAME];
    if (!parse_filename_token(args, filename, sizeof(filename), NULL)) {
        kprintf("Usage: rm <filename>\n");
        return;
    }

    if (fs_remove(filename)) {
        kprintf("Deleted %s\n", filename);
    } else {
        kprintf("File not found.\n");
    }
}

static void command_sysinfo(const char* args) {
    (void)args;

    const struct BootInfo* boot = system_boot_info();
    const struct system_profile* profile = system_profile_info();

    kprintf("Display:      %ux%u\n", boot->width, boot->height);
    kprintf("Framebuffer:  %p\n", (void*)boot->framebuffer);
    kprintf("Memory:       %u KiB used / %u KiB total\n", 
            profile->memory_used_kb, profile->memory_total_kb);
    kprintf("Arch:         %s\n", profile->architecture);
}

static void command_memtest(const char* args) {
    (void)args;
    memtest_run_diagnostic();
}

static void command_logs(const char* args) {
    (void)args;
    size_t count = syslog_length();
    if (count == 0) {
        kprintf("No log entries recorded yet.\n");
        return;
    }
    kprintf("Recent system logs:\n");
    for (size_t i = 0; i < count; i++) {
        const char* entry = syslog_entry(i);
        if (entry) kprintf("  %s\n", entry);
    }
}

static void command_snake(const char* args) {
    (void)args;
    snake_game_run();
    background_render();
    shell_print_banner();
}

static void command_beep(const char* args) {
    (void)args;
    kprintf("Beeping at 440Hz for 50 ticks...\n");
    sound_beep(440, 50);
    kprintf("Done.\n");
}

static void command_disktest(const char* args) {
    (void)args;
    kprintf("Initializing ATA driver...\n");
    if (!ata_init()) {
        kprintf("ATA init failed. Disk operations aborted.\n");
        return;
    }

    uint8_t buffer[512];
    
    // --- Read Test: Sector 0 (MBR) ---
    kprintf("Reading LBA 0 (Boot Sector)...\n");
    ata_read(0, 1, buffer);
    
    // Check boot signature at offset 510
    // Should be 0x55, 0xAA
    kprintf("Signature bytes: 0x%x 0x%x\n", buffer[510], buffer[511]);
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        kprintf("[PASS] Boot signature found.\n");
    } else {
        kprintf("[FAIL] Invalid signature.\n");
    }

    // --- Write Test: Sector 1000 (Safe Zone) ---
    // We pick a sector far enough to avoid the kernel code (assuming kernel < 500KB)
    uint32_t test_lba = 1000;
    const char* test_str = "RetroOS Disk Test Successful!";
    size_t len = kstrlen(test_str);

    kprintf("Writing test pattern to LBA %u...\n", test_lba);
    
    // Clear buffer
    for(int i=0; i<512; i++) buffer[i] = 0;
    // Fill
    for(size_t i=0; i<len; i++) buffer[i] = test_str[i];

    ata_write(test_lba, 1, buffer);

    // Verify
    kprintf("Verifying write...\n");
    // Clear buffer again
    for(int i=0; i<512; i++) buffer[i] = 0;
    
    ata_read(test_lba, 1, buffer);
    
    if (kstrcmp((char*)buffer, test_str) == 0) {
        kprintf("[PASS] Data verified: '%s'\n", buffer);
    } else {
        kprintf("[FAIL] Data mismatch.\n");
    }
}

static void command_reboot(const char* args) {
    (void)args;
    kprintf("Rebooting system...\n");
    uint8_t temp = 0x02;
    while (temp & 0x02) temp = inb(0x64);
    outb(0x64, 0xFE);
    for(;;) __asm__ volatile ("hlt");
}

static void command_shutdown(const char* args) {
    (void)args;
    kprintf("Shutting down...\n");
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "d"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "d"((uint16_t)0xB004));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x3400), "d"((uint16_t)0x4004));
    for(;;) __asm__ volatile ("cli; hlt");
}

static void command_banner(const char* args) {
    (void)args;
    banner_run();
    shell_print_banner();
}

static void log_command_invocation(const char* command_name) {
    (void)command_name;
    // Use kprintf logging style here if we updated syslog, but keep simple for now
    syslog_write("Command executed");
}

static void command_echo(const char* args) {
    const char* message = kskip_spaces(args);
    if (*message == '\0') {
        kprintf("Usage: echo <text>\n");
        return;
    }
    kprintf("%s\n", message);
}

static void execute_command(const char* input) {
    const char* trimmed = kskip_spaces(input);
    if (*trimmed == '\0') return;

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

    kprintf("Unknown command. Type 'help'.\n");
}

void shell_run(void) {
    char input[INPUT_CAPACITY];
    sound_init();
    
    // Automatically start the banner animation at boot
    // Waits for user keypress to continue
    banner_run();

    shell_print_banner();

    for (;;) {
        shell_print_prompt();
        keyboard_read_line(input, sizeof(input));
        execute_command(input);
    }
}
