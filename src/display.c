/*
 * display.c - screen buffer handling and rendering.
 *
 * The calculator has a 192x64 monochrome LCD, but the LCD controller's own
 * framebuffer is 256x64 (32 bytes per row).  Drawing happens in one of two
 * packed 24-bytes-per-row buffers and render_* spreads a buffer into the
 * controller framebuffer.
 */
#include "display.h"
#include "runtime.h"

/* 0x08C60 - clear buffer 1 */
void buffer_clear(uint8_t *ds)
{
    memset_n(ds, SCREEN_BUF1, 0, BUF_SIZE);
}

/* 0x09440 - buffer 1 -> buffer 2 */
void buf1_to_buf2(uint8_t *ds)
{
    memcpy_nn(ds, SCREEN_BUF2, SCREEN_BUF1, BUF_SIZE);
}

/* 0x09458 - buffer 2 -> buffer 1 */
void buf2_to_buf1(uint8_t *ds)
{
    memcpy_nn(ds, SCREEN_BUF1, SCREEN_BUF2, BUF_SIZE);
}

/*
 * 0x08C0C - fill screen buffers.
 *   low byte of `arg`  = fill value
 *   high byte of `arg` = flags:
 *      1 -> clear buffer 1 (0xDDD4)
 *      2 -> clear the LCD framebuffer (0xF800)
 *      4 -> clear buffer 2 (0xE3D4); only when flags == 4 exactly,
 *           matching the ROM's `cmp r5, 4 ; bc eq`
 */
void fill_screen(uint8_t *ds, uint16_t arg)
{
    uint8_t value = (uint8_t)(arg & 0xFF);
    uint8_t flags = (uint8_t)(arg >> 8);

    if (flags == 4) {
        memset_n(ds, SCREEN_BUF2, value, BUF_SIZE);
        return;
    }
    if (flags & 1)
        memset_n(ds, SCREEN_BUF1, value, BUF_SIZE);
    if (flags & 2)
        memset_n(ds, SCREEN_LCD, value, LCD_SIZE);
}

/*
 * 0x0B060 - request an LCD refresh.  Only does something when the "screen
 * off" flag at 0xD0F5 is clear and bit 0 of 0xD0F7 is set.
 */
void lcd_update(uint8_t *ds)
{
    if (ds[0xD0F5] != 0)
        return;
    if ((ds[0xD0F7] & 1) == 0)
        return;
    ds[0xF816] = 1;
}

/*
 * 0x09470 / 0x0947C - copy a packed 192-pixel-wide buffer into the LCD
 * framebuffer, then request a refresh.  Each row is 24 bytes in the buffer
 * and 32 bytes on the controller; 64 rows.
 */
static void render(uint8_t *ds, uint16_t buffer)
{
    uint16_t src = buffer;
    uint16_t dst = SCREEN_LCD;

    for (int row = 0; row < 64; row++) {
        memcpy_nn(ds, dst, src, 12);
        memcpy_nn(ds, (uint16_t)(dst + 12), (uint16_t)(src + 12), 12);
        src = (uint16_t)(src + 24);
        dst = (uint16_t)(dst + 32);
    }
    lcd_update(ds);
}

/* 0x0947C */
void render_ddd4(uint8_t *ds)
{
    render(ds, SCREEN_BUF1);
}

/* 0x09470 */
void render_e3d4(uint8_t *ds)
{
    render(ds, SCREEN_BUF2);
}
