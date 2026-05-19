#pragma once

#include <stdint.h>

/** Clear SH1106 page framebuffer (128x64, 1 bpp). */
void oled_font_clear(uint8_t *fb, int width, int height);

/** Draw ASCII string; y is baseline (bottom of glyphs). */
void oled_font_draw_str(uint8_t *fb, int x, int y, int width, int height, const char *str);
