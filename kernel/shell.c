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
#include "gui_demo.h"
#include "scheduler.h"

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
static void command_gui(const char* args);

static const struct shell_command COMMANDS[] = {
    {"help", command_help, "Show this help message"},
    {"about", command_about, "Learn more about " OS_NAME},
    {"clear", command_clear, "Clear the screen"},
    {"banner", command_banner, "Show moving banner screensaver"},
    {"gui", command_gui, "Launch Desktop Environment (User Mode)"},
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
    
    second = (second & 0x0F) + ((second / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    hour   = ((hour & 0x0F) + ((hour / 16) * 10));

    kprintf("RTC Time: %02u:%02u:%02u\n", hour, minute, second);
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
    if (!kparse_uint(&cursor, &a)) { kprintf("Usage: calc <num> <op> <num>\n"); return; }
    cursor = kskip_spaces(cursor);
    char op = *cursor++;
    cursor = kskip_spaces(cursor);
    if (!kparse_uint(&cursor, &b)) { kprintf("Usage: calc <num> <op> <num>\n"); return; }

    long result = 0;
    if (op == '+') result = (long)a + b;
    else if (op == '-') result = (long)a - b;
    else if (op == '*') result = (long)a * b;
    else if (op == '/') {
        if (b == 0) { kprintf("Error: Div by zero\n"); return; }
        result = (long)a / b;
    } else { kprintf("Unknown operator\n"); return; }
    
    kprintf("Result: %d\n", (int)result);
}

static int resolve_color_name(const char* input, const char** end_ptr) {
    int best_match = -1;
    size_t best_len = 0;
    for (int i = 0; i < 16; i++) {
        const char* name = COLOR_NAMES[i];
        size_t name_len = kstrlen(name);
        if (kstrncmp(input, name, name_len) == 0) {
             if (name_len > best_len) { best_match = i; best_len = name_len; }
        }
    }
    if (best_match != -1) { if(end_ptr) *end_ptr = input + best_len; return best_match; }
    return -1;
}

static bool parse_color_arg(const char** cursor, int* out_color) {
    *cursor = kskip_spaces(*cursor);
    const char* tmp = *cursor;
    unsigned int val;
    if (kparse_uint(&tmp, &val) && val < 16) { *out_color = val; *cursor = tmp; return true; }
    int idx = resolve_color_name(*cursor, &tmp);
    if (idx != -1) { *out_color = idx; *cursor = tmp; return true; }
    return false;
}

static void command_foreground(const char* args) {
    int fg; const char* c = args;
    if (parse_color_arg(&c, &fg)) { 
        uint8_t ofg, obg; terminal_getcolors(&ofg, &obg);
        terminal_set_theme(fg, obg); 
    } else kprintf("Usage: foreground <color>\n");
}

static void command_background(const char* args) {
    int bg; const char* c = args;
    if (parse_color_arg(&c, &bg)) { 
        uint8_t ofg, obg; terminal_getcolors(&ofg, &obg);
        terminal_set_theme(ofg, bg); 
    } else kprintf("Usage: background <color>\n");
}

static void shell_print_banner(void) {
    kprintf("%s\n%s\nType 'help' for commands.\n", OS_BANNER_LINE, OS_WELCOME_LINE);
}

static void shell_print_prompt(void) {
    terminal_newline();
    kprintf(OS_PROMPT_TEXT);
}

// --- COMMANDS ---

static void command_help(const char* args) {
    (void)args;
    kprintf("Available commands:\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        kprintf("  %s", COMMANDS[i].name);
        size_t len = kstrlen(COMMANDS[i].name);
        while (len++ < 12) terminal_write_char(' ');
        kprintf("- %s\n", COMMANDS[i].description);
    }
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
    (void)args;
    size_t count = keyboard_history_length();
    kprintf("History:\n");
    for (size_t i = 0; i < count; i++) {
        kprintf("%u. %s\n", (unsigned int)(i+1), keyboard_history_entry(i));
    }
}

static void command_ls(const char* args) {
    (void)args;
    size_t count = fs_file_count();
    kprintf("Files (%u):\n", count);
    for (size_t i = 0; i < count; i++) {
        const struct fs_file* f = fs_file_at(i);
        if(f) kprintf("  %s (%u bytes)\n", f->name, f->size);
    }
}

static void command_cat(const char* args) {
    const char* name = kskip_spaces(args);
    const struct fs_file* f = fs_find(name);
    if (f) { terminal_writestring(f->data); terminal_newline(); }
    else kprintf("File not found.\n");
}

static void command_hexdump(const char* args) {
    const char* name = kskip_spaces(args);
    const struct fs_file* f = fs_find(name);
    if (!f) { kprintf("File not found.\n"); return; }
    
    for(size_t i=0; i<f->size; i++) {
        if(i%16==0) kprintf("\n%04x: ", i);
        kprintf("%02x ", (unsigned char)f->data[i]);
    }
    terminal_newline();
}

static void command_touch(const char* args) {
    const char* name = kskip_spaces(args);
    if(fs_touch(name)) kprintf("Created %s\n", name);
    else kprintf("Failed.\n");
}

static void command_write(const char* args) {
    (void)args;
    kprintf("Usage: write <file> <content> (Not implemented fully in this snippet)\n");
}

static void command_append(const char* args) {
    (void)args;
    kprintf("Usage: append <file> <content> (Not implemented fully in this snippet)\n");
}

static void command_rm(const char* args) {
    const char* name = kskip_spaces(args);
    if(fs_remove(name)) kprintf("Removed %s\n", name);
    else kprintf("Failed.\n");
}

static void command_sysinfo(const char* args) {
    (void)args;
    const struct BootInfo* boot = system_boot_info();
    const struct system_profile* prof = system_profile_info();
    kprintf("Res: %ux%u | Mem: %uKB\n", boot->width, boot->height, prof->memory_total_kb);
}

static void command_memtest(const char* args) {
    (void)args;
    memtest_run_diagnostic();
}

static void command_logs(const char* args) {
    (void)args;
    size_t count = syslog_length();
    for(size_t i=0; i<count; i++) kprintf("[%u] %s\n", i, syslog_entry(i));
}

static void command_snake(const char* args) {
    (void)args;
    timer_set_callback(NULL); 
    snake_game_run();
    background_render();
    shell_print_banner();
    timer_set_callback(background_animate); 
}

static void command_beep(const char* args) {
    (void)args;
    sound_beep(440, 20);
}

static void command_disktest(const char* args) {
    (void)args;
    if(ata_init()) kprintf("ATA Init OK.\n");
    else kprintf("ATA Init Failed.\n");
}

static void command_reboot(const char* args) {
    (void)args;
    outb(0x64, 0xFE);
}

static void command_shutdown(const char* args) {
    (void)args;
    kprintf("Shutting down...\n");
    outw(0x604, 0x2000); 
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    for(;;) __asm__ volatile ("cli; hlt");
}

static void command_banner(const char* args) {
    (void)args;
    timer_set_callback(NULL);
    banner_run();
    timer_set_callback(background_animate);
    shell_print_banner();
}

// Wrapper function to satisfy the scheduler's task entry point signature
static void user_gui_wrapper(void) {
    // Running in Ring 3 (User Mode)
    timer_set_callback(NULL);
    gui_demo_run();
    
    // FIX: Use pause instead of hlt loop to avoid GPF in User Mode
    while(1) __asm__ volatile("pause");
}

static void command_gui(const char* args) {
    (void)args;
    kprintf("Launching GUI in User Mode (Ring 3)...\n");
    
    // Spawn the task
    spawn_user_task(user_gui_wrapper);
    
    // Halt the kernel shell to prevent it from stealing input.
    // We use a yield loop (wait) instead of hlt just to be safe, 
    // though shell is Ring 0 so hlt is technically allowed.
    while(true) {
        timer_wait(100);
    }
}

static void command_echo(const char* args) {
    kprintf("%s\n", kskip_spaces(args));
}

static void log_command_invocation(const char* command_name) {
    (void)command_name;
}

static void execute_command(const char* input) {
    const char* trimmed = kskip_spaces(input);
    if (*trimmed == '\0') return;
    keyboard_history_record(trimmed);

    char cmd_name[32];
    size_t i = 0;
    while(trimmed[i] && trimmed[i] != ' ' && i < 31) {
        cmd_name[i] = trimmed[i];
        i++;
    }
    cmd_name[i] = '\0';
    
    const char* args = trimmed + i;

    for (size_t j = 0; j < COMMAND_COUNT; j++) {
        if (kstrcmp(cmd_name, COMMANDS[j].name) == 0) {
            log_command_invocation(COMMANDS[j].name);
            COMMANDS[j].handler(args);
            return;
        }
    }
    kprintf("Unknown command.\n");
}

void shell_run(void) {
    char input[INPUT_CAPACITY];
    sound_init();
    shell_print_banner();

    for (;;) {
        shell_print_prompt();
        keyboard_read_line_ex(input, sizeof(input), NULL);
        execute_command(input);
    }
}
