#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>

void display_manager_init(void);
void display_manager_fill_screen(uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);
void display_manager_flush(void);

#endif // DISPLAY_MANAGER_H
