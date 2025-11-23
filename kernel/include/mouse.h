#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int x;
    int y;
    bool left_button;
    bool right_button;
} MouseState;

void mouse_init(void);
void mouse_handle_interrupt(void);
MouseState mouse_get_state(void);

#endif
