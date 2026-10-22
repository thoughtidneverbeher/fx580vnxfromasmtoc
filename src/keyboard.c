/*
 * keyboard.c - keyboard scanning and timer programming.
 *
 * The keyboard is a matrix: driving a value into 0xF046 selects a column and
 * the rows then appear on 0xF040 with pressed keys reading as clear bits.
 * Scanning waits for the contact to settle by programming the built-in timer,
 * and the "double programming" of the prescaler register is what the firmware
 * really does.
 */
#include "keyboard.h"

#include "../src/runtime.h"

static void wr16(uint8_t *ds, uint16_t a, uint16_t v)
{
    ds[a] = (uint8_t)(v & 0xFF);
    ds[(uint16_t)(a + 1)] = (uint8_t)(v >> 8);
}

void timer_delay(uint8_t *ds, uint16_t interval)
{
    /* The firmware masks and unmask interrupts around the timer setup; that
     * has no effect on the data space, so it is not modelled here. */
    if (ds[SFR_0F00A] & 2)
        ds[SFR_0F00A] &= (uint8_t)~2;

    wr16(ds, SFR_TIMER_PRESCALE, 1);
    wr16(ds, SFR_TIMER_COUNTER, 0);
    wr16(ds, SFR_TIMER_INTERVAL, interval);
    ds[SFR_TIMER_CONTROL] = 1;
    ds[SFR_0F014] = 0;
    ds[SFR_0F015] = 0;
    ds[SFR_0F008] = 80;
    ds[SFR_0F008] = 160;
    ds[SFR_0F009] = 2;

    /* When the key filter was not armed the whole sequence is repeated with
     * the firmware's short interval instead of the requested one. */
    if (ds[SFR_KEY_IN_FILTER] != 0)
        return;

    wr16(ds, SFR_TIMER_PRESCALE, 15);
    wr16(ds, SFR_TIMER_COUNTER, 0);
    wr16(ds, SFR_TIMER_INTERVAL, 0x079E);
    ds[SFR_TIMER_CONTROL] = 1;
    ds[SFR_0F014] = 0;
    ds[SFR_0F015] = 0;
}

void key_scan_end(uint8_t *ds)
{
    ds[RAM_0D13A] = 0;
}

uint8_t check_any_key_pressed(uint8_t *ds, uint16_t desc)
{
    uint8_t found = 0;

    ds[SFR_KEY_OUTPUT] = ds[(uint8_t)(desc + 1)];
    for (uint8_t n = 1; n <= 10; n++) {
        uint8_t rows = (uint8_t)(ds[SFR_KEY_INPUT] ^ 0xFF);

        if ((uint8_t)(rows & ds[desc]) != 0) {
            found = 1;
            break;
        }
        timer_delay(ds, 10);
    }
    ds[SFR_KEY_OUTPUT] = 0;
    if (!found)
        key_scan_end(ds);
    return found;
}

void timer_prescale_setup(uint8_t *ds)
{
    ds[0xF010] = 34;
    ds[0xF011] = 0;
    ds[0xF012] = 0;
    ds[0xF018] = 3;
}

void key_filter_clear(uint8_t *ds)
{
    ds[SFR_KEY_IN_FILTER] = 0;
}

void key_output_all(uint8_t *ds)
{
    ds[SFR_KEY_OUTPUT] = 127;
}

void key_output_zero(uint8_t *ds)
{
    ds[SFR_KEY_OUTPUT] = 0;
}

uint8_t key_all_released(uint8_t *ds)
{
    /* The firmware spins 15 times on a one-byte countdown first; that busy
     * wait touches nothing in the data space, so it is not modelled. */
    ds[SFR_KEY_OUTPUT] = 127;
    /* `cmp ki, 255 ; addc r0, 0` sets the result when 255 > ki, i.e. as soon
     * as any row reads back as a clear bit (a key is still held). */
    return ds[SFR_KEY_INPUT] < 255 ? 1 : 0;
}

void keyboard_reset(uint8_t *ds)
{
    ds[SFR_KEY_OUT_MASK] = 128;
    ds[0xF045] = 255;
}

void hw_clear_0F220(uint8_t *ds)
{
    ds[0xF220] = 0;
    ds[0xF221] = 127;
    ds[0xF222] = 0;
    ds[0xF223] = 127;
    ds[0xF224] = 0;
}

void hw_clear_0F048(uint8_t *ds)
{
    ds[0xF048] = 0;
    ds[0xF049] = 0;
    ds[0xF04A] = 7;
    ds[0xF04B] = 0;
    ds[0xF04C] = 7;
}

void lcd_hw_setup(uint8_t *ds)
{
    ds[0xF030] = 4;   /* range */
    ds[0xF033] = 7;   /* contrast 2 */
    ds[0xF034] = 9;
    ds[0xF035] = 23;
    ds[0xF036] = 1;
    ds[0xF039] = 0;   /* r1 is still 0 from `mov er0, 4` */
    ds[0xF031] = 23;  /* mode */
    ds[0xF032] = 20;  /* contrast */
}
