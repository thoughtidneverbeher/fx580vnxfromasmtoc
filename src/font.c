/*
 * font.c - bit-packed font glyph extraction.
 *
 * The fonts are stored as bit streams.  A 16-character group occupies
 * `slots` words per row; consecutive slots are stored `rows * 2` bytes apart
 * (slot-major layout), and the rows inside a slot are consecutive words.
 * A glyph is a bit window `bits` wide at bit offset (c-16)*bits, so it may
 * straddle two adjacent slots.
 */
#include "font.h"

#include "../src/runtime.h"

#define FONT_SIZE_ADDR 0xD137

static uint16_t rd16(const uint8_t *ds, uint16_t a)
{
    return (uint16_t)(ds[a] | ((uint16_t)ds[a + 1] << 8));
}

/* Wide glyph markers use 11-bit cells and 12 rows. */
static int is_wide(uint8_t marker)
{
    return marker == 241 || marker == 242;
}

/*
 * Core of 0x09238: extract a glyph into host memory, 2 bytes per row.
 * `out` must have room for FONT14_ROWS * 2 bytes.
 */
void char_get_14_buf(uint8_t *ds, uint8_t c, uint8_t marker, uint8_t *out)
{
    uint16_t base;
    int rows = is_wide(marker) ? FONT14_WIDE_ROWS : FONT14_ROWS;
    int bits = is_wide(marker) ? 11 : 10;

    if (marker == 0)
        base = 0x0AE2;
    else
        base = rd16(ds, (uint16_t)(0x38AA + 2 * (marker & 15)));

    /* '|' in the normal font is a solid block. */
    if (marker == 0 && c == 124) {
        for (int i = 0; i < rows; i++) {
            out[2 * i] = 0x00;
            out[2 * i + 1] = 0xE0;
        }
        return;
    }

    /* 8-bit multiply, as the ROM does (mul truncates its first operand). */
    uint16_t x = (uint16_t)((uint8_t)(c - 16) * bits);
    uint8_t slot = (uint8_t)(x >> 4);
    uint8_t bit = (uint8_t)(x & 15);
    uint16_t ea = (uint16_t)(base + slot * rows * 2);

    for (int row = 0; row < rows; row++) {
        uint16_t w0 = rd16(ds, (uint16_t)(ea + 2 * row));
        uint16_t w1 = rd16(ds, (uint16_t)(ea + rows * 2 + 2 * row));
        uint32_t v = (uint32_t)w0 << bit;
        if (bit)
            v |= (uint32_t)w1 >> (16 - bit);
        v &= 0xFFFFu;
        v &= is_wide(marker) ? 0xFFE0u : 0xFFC0u;
        v >>= 1;
        out[2 * row] = (uint8_t)(v & 0xFF);
        out[2 * row + 1] = (uint8_t)(v >> 8);
    }
}

uint16_t char_get_14(uint8_t *ds, uint8_t c, uint8_t marker, uint16_t out)
{
    uint8_t tmp[2 * FONT14_ROWS];
    int rows = is_wide(marker) ? FONT14_WIDE_ROWS : FONT14_ROWS;

    char_get_14_buf(ds, c, marker, tmp);
    for (int i = 0; i < 2 * rows; i++)
        ds[out + i] = tmp[i];
    return out;
}

/*
 * Core of 0x09318: extract a glyph into host memory, 1 byte per row.
 * `out` must have room for FONT10_ROWS bytes.
 */
void char_get_l14_buf(uint8_t *ds, uint8_t c, uint8_t marker, uint8_t *out)
{
    uint16_t base;
    int rows;

    if (ds[FONT_SIZE_ADDR] == 8) {
        base = 0x023A;
        rows = FONT8_ROWS;
    } else {
        if (marker == 0)
            base = 0x462A;
        else
            base = rd16(ds, (uint16_t)(0x38C8 + 2 * (marker & 15)));
        rows = FONT10_ROWS;
    }

    if (marker == 0 && c == 124) {
        for (int i = 0; i < rows; i++)
            out[i] = 0xE0;
        return;
    }

    uint16_t x = (uint16_t)((uint8_t)(c - 16) * 5);
    uint8_t slot = (uint8_t)(x >> 3);
    uint8_t bit = (uint8_t)(x & 7);
    uint16_t ea = (uint16_t)(base + slot * rows);

    for (int row = 0; row < rows; row++) {
        uint8_t v = (uint8_t)(ds[ea + row] << bit);
        v >>= 1;
        if (8 - bit < 5) {
            uint8_t next = ds[ea + rows + row];
            v |= (uint8_t)(next >> (9 - bit));
        }
        out[row] = (uint8_t)(v & 0x7C);
    }
}

uint16_t char_get_l14(uint8_t *ds, uint8_t c, uint8_t marker, uint16_t out)
{
    uint8_t tmp[FONT10_ROWS];
    int rows = ds[FONT_SIZE_ADDR] == 8 ? FONT8_ROWS : FONT10_ROWS;

    char_get_l14_buf(ds, c, marker, tmp);
    for (int i = 0; i < rows; i++)
        ds[out + i] = tmp[i];
    return out;
}
