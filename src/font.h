/* font.h - bit-packed font glyph extraction. */
#ifndef NX_FONT_H
#define NX_FONT_H

#include <stdint.h>

/* Glyph cell geometries. */
#define FONT14_ROWS      13
#define FONT14_WIDE_ROWS 12
#define FONT10_ROWS      9
#define FONT8_ROWS       7

/* 0x09238: extract a glyph from the 14-pixel font.
 *   c      character code
 *   marker 0 = normal font, 241/242 = wide symbol font (11 bits, 12 rows)
 *   out    destination for FONT14_ROWS (13) 16-bit rows, big-endian bit order
 * Returns the advanced destination pointer. */
uint16_t char_get_14(uint8_t *ds, uint8_t c, uint8_t marker, uint16_t out);

/* Host-buffer variants used by char_print; `out` holds 2 bytes per row
 * (char_get_14_buf) or 1 byte per row (char_get_l14_buf). */
void char_get_14_buf(uint8_t *ds, uint8_t c, uint8_t marker, uint8_t *out);
void char_get_l14_buf(uint8_t *ds, uint8_t c, uint8_t marker, uint8_t *out);

/* 0x09318: extract a glyph from the 10- or 8-pixel font (selected by the
 * font_size byte at 0xD137).  Each row is one byte, 5 bits per glyph. */
uint16_t char_get_l14(uint8_t *ds, uint8_t c, uint8_t marker, uint16_t out);

#endif
