#include "kstdio.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "terminal.h"
#include "kstring.h"

static void print_uint(unsigned long long value, int base, bool pad_pointer) {
    char buffer[32];
    int pos = 0;
    const char* digits = "0123456789abcdef";

    // Handle 0 explicitly
    if (value == 0) {
        if (pad_pointer) {
            // For %p NULL, often nice to print (nil) or 0x00...00,
            // but strictly we just print 0 or 0x0. 
            // Let's behave like %x for 0, but ensure pointer padding logic if requested.
        }
        terminal_write_char('0');
        return;
    }

    while (value > 0) {
        buffer[pos++] = digits[value % base];
        value /= base;
    }

    // Print in reverse
    while (pos > 0) {
        terminal_write_char(buffer[--pos]);
    }
}

static void print_int(long long value) {
    if (value < 0) {
        terminal_write_char('-');
        value = -value;
    }
    print_uint((unsigned long long)value, 10, false);
}

void kvprintf(const char* format, va_list args) {
    terminal_begin_batch();

    while (*format != '\0') {
        if (*format != '%') {
            terminal_write_char(*format);
            format++;
            continue;
        }

        format++; // Skip '%'
        if (*format == '\0') break;

        switch (*format) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (s == NULL) s = "(null)";
                terminal_writestring(s);
                break;
            }
            case 'c': {
                // char is promoted to int in varargs
                char c = (char)va_arg(args, int);
                terminal_write_char(c);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                print_int((long long)d);
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                print_uint((unsigned long long)u, 10, false);
                break;
            }
            case 'x': {
                // We'll accept standard int/unsigned for %x, 
                // but handle larger types if standard promotion applies.
                unsigned long long x = va_arg(args, unsigned int);
                terminal_writestring("0x");
                print_uint(x, 16, false);
                break;
            }
            case 'p': {
                void* ptr = va_arg(args, void*);
                terminal_writestring("0x");
                print_uint((uintptr_t)ptr, 16, true);
                break;
            }
            case '%': {
                terminal_write_char('%');
                break;
            }
            default: {
                // Unknown format specifier, print it literally
                terminal_write_char('%');
                terminal_write_char(*format);
                break;
            }
        }
        format++;
    }

    terminal_end_batch();
}

void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    kvprintf(format, args);
    va_end(args);
}
