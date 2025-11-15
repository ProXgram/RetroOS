#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

char keyboard_get_char(void);
void keyboard_read_line(char* buffer, size_t size);

#endif /* KEYBOARD_H */
