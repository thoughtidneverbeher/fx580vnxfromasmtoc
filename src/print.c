/*
 * print.c - character drawing.
 *
 * The screen is a 192x64 monochrome bitmap stored as 24 bytes per row with
 * the leftmost pixel in bit 7.  Glyphs come out of the font extractors as
 * 16-bit rows (14-pixel font) or 8-bit rows (10/8-pixel fonts), MSB first,
 * and are merged into the buffer with a per-bit mask.
 */
#include "print.h"

#include "../src/runtime.h"
#include "font.h"

#define FONT_SIZE_ADDR   0xD137
#define DRAW_MODE_ADDR   0xD138
#define CURRENT_BUF_ADDR 0xD139

/* Mask tables in the ROM.  0x0388C keeps the high (8-bit) bits of a byte,
 * 0x03894 keeps the low (bit+1) bits. */
#define MASK_HI_ADDR 0x388Cu
#define MASK_LO_ADDR 0x3894u

/* Control character 1 in a printed string.  The ROM asks f_10E3A for an
 * index, which reads one byte through data segment 3 (0x3FFDC); the flat
 * 64 KiB data window used here cannot express that address, so the constant
 * is captured directly. */
#define CONTENT_1_INDEX 3u
/* d_038A4: character used for control character 1, indexed by the value above. */
#define CONTENT_1_CHARS 0x38A4u
/* f_29978 always returns the width of one control-character-1 cell. */
#define CONTENT_1_STEP 4u
#define FLAG_0D11C 0xD11Cu
#define MODE_0D111 0xD111u

void screen_blend(uint8_t *ds, uint16_t addr, uint8_t value, uint8_t mask)
{
    uint8_t old;

    if (addr <= DRAW_MIN_ADDR)
        return;
    if (mask != 0)
        value &= mask;

    old = ds[addr];
    switch (ds[DRAW_MODE_ADDR]) {
    case 0:                     /* replace masked bits */
        ds[addr] = (uint8_t)((old & (uint8_t)~mask) | value);
        break;
    case 1:                     /* set */
        ds[addr] = (uint8_t)(old | value);
        break;
    case 2:                     /* clear */
        ds[addr] = (uint8_t)(old & (uint8_t)((uint8_t)~mask | value));
        break;
    case 3:                     /* invert */
        ds[addr] = (uint8_t)(old ^ value);
        break;
    case 4:                     /* replace with the complement */
        ds[addr] = (uint8_t)((old & (uint8_t)~mask) |
                             ((uint8_t)~value & mask));
        break;
    default:                    /* the ROM falls through to XOR */
        ds[addr] = (uint8_t)(old ^ value);
        break;
    }
}

static uint16_t buffer_addr(uint16_t base, uint8_t stride, uint16_t xy,
                            uint16_t *out)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    /* y >= 0x80 addresses rows above the buffer (the ROM negates it). */
    int16_t row = (int8_t)y;
    uint16_t a = (uint16_t)(base + stride * row + (x >> 3));

    *out = a;
    return (uint16_t)(x & 7);
}

uint16_t screen_addr(uint8_t *ds, uint16_t xy, uint16_t *out)
{
    uint16_t base = ds[CURRENT_BUF_ADDR] != 0 ? 0xDDD4u : 0xE3D4u;

    return buffer_addr(base, SCREEN_ROW_BYTES, xy, out);
}

uint16_t lcd_addr(uint8_t *ds, uint16_t xy, uint16_t *out)
{
    (void)ds;
    return buffer_addr(0xF800u, 32, xy, out);
}

void char_print(uint8_t *ds, uint16_t xy, uint8_t ch, uint8_t marker)
{
    uint8_t x = (uint8_t)xy;
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t font = ds[FONT_SIZE_ADDR];
    uint8_t glyph[2 * FONT14_ROWS];
    uint8_t width, threshold, height, stride, mid_mask;
    uint8_t bit, mask_a, mask_b, counter;
    uint16_t screen, end, glyph_pos;
    int blank = 0;

    if (x > 191)
        return;
    if ((int8_t)y >= 64)
        return;

    if (marker == 241 || marker == 242) {
        width = 12;
        threshold = 4;
        height = (uint8_t)(font - 2);
    } else if (font == 14) {
        width = 11;
        threshold = 5;
        height = (uint8_t)(font - 1);
    } else {
        width = 6;
        threshold = 2;
        height = (uint8_t)(font - 1);
    }

    end = ds[CURRENT_BUF_ADDR] != 0 ? 0xE3D3u : 0xE9D3u;

    if (font > 10)
        char_get_14_buf(ds, ch, marker, glyph);
    else
        char_get_l14_buf(ds, ch, marker, glyph);

    bit = (uint8_t)screen_addr(ds, xy, &screen);
    mask_a = ds[MASK_HI_ADDR + bit];
    mask_b = ds[MASK_LO_ADDR + ((width + x) & 7)];
    stride = font > 10 ? 2 : 1;
    mid_mask = font > 10 ? 0xFF : threshold;

    glyph_pos = 0;
    counter = 0;
    for (;;) {
        const uint8_t *row = blank ? ds + BLANK_GLYPH : glyph + glyph_pos;

        if (stride == 1) {
            if (bit == 0) {
                screen_blend(ds, screen, row[0], mask_b);
            } else {
                uint8_t v = (uint8_t)(row[0] >> bit);
                int shift = 8 - width - bit;

                if (shift > 0)
                    mask_a &= (uint8_t)(0xFF << shift);
                screen_blend(ds, screen, (uint8_t)(v & mask_a), mask_a);
                if (bit >= threshold + 1) {
                    uint8_t v2 = (uint8_t)(row[0] << (8 - bit));

                    screen_blend(ds, (uint16_t)(screen + 1),
                                 (uint8_t)(v2 & mask_b), mask_b);
                }
            }
        } else {
            uint16_t v = (uint16_t)(row[0] | ((uint16_t)row[1] << 8));
            uint16_t shifted = (uint16_t)(v >> bit);
            uint8_t lo = (uint8_t)shifted;
            uint8_t hi = (uint8_t)(shifted >> 8);

            screen_blend(ds, screen, hi, mask_a);
            if (bit < threshold + 1) {
                screen_blend(ds, (uint16_t)(screen + 1), lo, mask_b);
            } else {
                screen_blend(ds, (uint16_t)(screen + 1), lo, mid_mask);
                screen_blend(ds, (uint16_t)(screen + 2),
                             (uint8_t)(row[0] << (8 - bit)), mask_b);
            }
        }

        screen = (uint16_t)(screen + SCREEN_ROW_BYTES);
        if (screen > end)
            return;
        glyph_pos = (uint16_t)(glyph_pos + stride);
        counter++;
        if (counter == height)
            blank = 1;
        if (counter > height)
            return;
    }
}

/*
 * 0x08FF6 - control character 1 inside a string.  Draws one cell (in "set
 * pixels" mode) just to the left of the cursor and advances the line by a
 * fixed amount.  Returns the new x coordinate.
 */
static uint8_t print_control_1(uint8_t *ds, uint16_t xy, uint8_t marker)
{
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t x = (uint8_t)xy;
    uint8_t index = CONTENT_1_INDEX;
    uint8_t flag = ds[FLAG_0D11C];
    uint8_t ch, save, offset;

    if (index == 1)
        flag ^= 1;
    if (flag == 0 || ds[MODE_0D111] == 2)
        ch = ' ';
    else
        ch = ds[(uint8_t)(CONTENT_1_CHARS + index)];

    save = ds[DRAW_MODE_ADDR];
    ds[DRAW_MODE_ADDR] = 1;
    offset = (uint8_t)(x - (ds[FONT_SIZE_ADDR] == 14 ? 2 : 1));
    char_print(ds, (uint16_t)((y << 8) | offset), ch, marker);
    ds[DRAW_MODE_ADDR] = save;

    return (uint8_t)(x + CONTENT_1_STEP);
}

uint8_t line_print(uint8_t *ds, uint16_t xy, uint16_t str, uint16_t *str_out)
{
    uint8_t y = (uint8_t)(xy >> 8);
    uint8_t x = (uint8_t)xy;
    uint8_t width = ds[FONT_SIZE_ADDR] == 14 ? 11 : 6;
    uint8_t limit = (uint8_t)(192 - width);
    uint8_t count = 0;
    uint8_t marker = 0;

    while (x <= limit) {
        uint8_t c = ds[str];

        if (c == 0)
            break;
        if (c >= 240) {
            /* Symbol-font markers are consumed silently and become the
             * marker of the next character. */
            marker = c;
        } else if (c == 1) {
            x = print_control_1(ds, (uint16_t)((y << 8) | x), marker);
            count++;
            marker = 0;
        } else if (c == 4) {
            /* In the 14-pixel font, byte 4 escapes the next character so it
             * is drawn nine pixels earlier.  Other fonts drop the byte. */
            if (ds[FONT_SIZE_ADDR] == 14) {
                str++;
                char_print(ds, (uint16_t)((y << 8) | x), ds[str], marker);
                x += 9;
                count++;
                marker = 0;
            }
        } else {
            char_print(ds, (uint16_t)((y << 8) | x), c, marker);
            x += (marker == 241 || marker == 242) ? 12 : width;
            count++;
            marker = 0;
        }
        str++;
    }

    if (str_out)
        *str_out = str;
    return count;
}

uint16_t str_decompress_print(uint8_t *ds, uint16_t str, uint16_t buf,
                              uint16_t *end_out)
{
    uint16_t src = str;
    uint16_t dst = buf;
    uint16_t out = buf;
    uint8_t count;

    while (ds[src] != 0) {
        uint8_t tag = ds[src];

        if (tag == 2 || tag == 4) {
            uint16_t index;

            src++;
            /* Tag 4 references the string tables with the high byte set (and
             * its index one lower), tag 2 references the low table. */
            index = tag == 4 ? (uint16_t)(0x0100u | (uint8_t)(ds[src] - 1))
                             : ds[src];
            dst = (uint16_t)(dst + copy_string_constant(ds, index, dst));
        } else {
            ds[dst++] = tag;
        }
        src++;
    }
    ds[dst] = 0;

    /* Too long for the frame: the ROM asks for a copy into the static buffer
     * at 0x1DB4, but passes the two arguments the other way round, so the
     * frame is overwritten with the "ERROR" string kept there.  Printing
     * always continues from the frame. */
    if (smart_strlen_n(ds, out) > DECOMP_MAX)
        smart_strcpy_nn(ds, out, DECOMP_STATIC);

    count = line_print(ds, 0, out, NULL);
    if (end_out)
        *end_out = (uint16_t)(str + smart_strlen_n(ds, str));
    return count;
}
