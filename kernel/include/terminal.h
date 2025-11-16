#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>
#include <stdint.h>

void terminal_initialize(uint32_t width, uint32_t height);
void terminal_clear(void);
void terminal_setcolors(uint8_t fg, uint8_t bg);
void terminal_getcolors(uint8_t* fg, uint8_t* bg);
void terminal_begin_batch(void);
void terminal_end_batch(void);
void terminal_write_char(char c);
void terminal_write(const char* data, size_t length);
void terminal_writestring(const char* data);
void terminal_write_uint(unsigned int value);
void terminal_newline(void);
void terminal_move_cursor_left(size_t count);
void terminal_move_cursor_right(size_t count);

#endif /* TERMINAL_H */
