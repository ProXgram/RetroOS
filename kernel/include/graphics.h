#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

void graphics_init(void);
void graphics_put_pixel(int x, int y, uint32_t color);
void graphics_fill_rect(int x, int y, int w, int h, uint32_t color);
void graphics_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

uint32_t graphics_get_width(void);
uint32_t graphics_get_height(void);

#endif
