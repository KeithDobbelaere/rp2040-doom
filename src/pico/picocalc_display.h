#pragma once

#include <stdint.h>

#define PICOCALC_LCD_W 320
#define PICOCALC_LCD_H 320
#define PICOCALC_DOOM_W 320
#define PICOCALC_DOOM_H 200
#define PICOCALC_DOOM_Y 60

#define PICOCALC_DMA_SLAB_ROWS 8

void picocalc_display_init(void);
void picocalc_display_begin_frame(void);
uint16_t *picocalc_display_acquire_row_buffer(int doom_y);
void picocalc_display_submit_row_buffer(int doom_y, uint16_t *row565);

void picocalc_display_write_doom_row(int doom_y, const uint16_t *row565);
void picocalc_display_end_frame(void);