#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

enum {
    KEYBOARD_MAX_LINE = 128,
    KEYBOARD_HISTORY_LIMIT = 16,
};

char keyboard_get_char(void);
void keyboard_read_line(char* buffer, size_t size);

void keyboard_history_record(const char* line);
size_t keyboard_history_length(void);
const char* keyboard_history_entry(size_t index);
const char* keyboard_history_step(int direction);
void keyboard_history_reset_iteration(void);

#endif /* KEYBOARD_H */
