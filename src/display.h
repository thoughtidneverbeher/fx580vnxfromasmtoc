/* display.h - screen buffer and rendering routines. */
#ifndef NX_DISPLAY_H
#define NX_DISPLAY_H

#include <stdint.h>

/* memory-mapped objects (data space offsets) */
#define SCREEN_BUF1 0xDDD4u   /* 192x64 monochrome buffer 1 */
#define SCREEN_BUF2 0xE3D4u   /* 192x64 monochrome buffer 2 */
#define SCREEN_LCD  0xF800u   /* LCD controller framebuffer (256x64) */
#define BUF_SIZE    0x0600u
#define LCD_SIZE    0x0800u

void buffer_clear(uint8_t *ds);              /* 0x08C60 */
void fill_screen(uint8_t *ds, uint16_t arg); /* 0x08C0C */
void buf1_to_buf2(uint8_t *ds);              /* 0x09440 */
void buf2_to_buf1(uint8_t *ds);              /* 0x09458 */
void render_ddd4(uint8_t *ds);               /* 0x0947C */
void render_e3d4(uint8_t *ds);               /* 0x09470 */
void lcd_update(uint8_t *ds);                /* 0x0B060 */

#endif
