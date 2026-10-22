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

/* byte_position - the ROM numbers the keyboard matrix by "how many times the
 * byte shifts right before it reaches zero", i.e. the position of the highest
 * set bit, counted from one.  A multi-bit byte takes the position of its top
 * bit, exactly like the `srl` loop in the original code does. */
static uint8_t byte_position(uint8_t v)
{
    uint8_t n = 0;

    while (v > 0) {
        n++;
        v >>= 1;
    }
    return n;
}

/* 0x29892 - keycode_cvt.  Reads the two scanner bytes at key_storage
 * ([0] = column/KO, [1] = row/KI), maps the pair to an index in the keycode
 * table and returns the 16-bit logical keycode stored there.  Either byte
 * being zero (no key pressed in that direction) yields 0.  Pure function:
 * nothing is written back to the storage pair. */
uint16_t keycode_cvt(uint8_t *ds, uint16_t key_storage, uint16_t table)
{
    uint8_t col = ds[key_storage];
    uint8_t row = ds[key_storage + 1];
    uint16_t index;
    uint16_t entry;

    if (row == 0 || col == 0)
        return 0;

    index = (uint16_t)((byte_position(col) - 1) << 3);
    index = (uint16_t)(index + byte_position(row) - 1);

    entry = (uint16_t)(table + 2 * index);
    return (uint16_t)(ds[entry] | ((uint16_t)ds[entry + 1] << 8));
}

/* 0x23CA4 - wait 10 timer ticks.  The push/pop of xr0 around the call keeps
 * the caller's er0; in this model timer_delay takes no values anyway. */
void wait_10_ticks(uint8_t *ds)
{
    timer_delay(ds, 10);
}

/* 0x0A058 / 0x0A05C - `ei; rt` and `di; rt`.  Nothing in the data space
 * moves; the stubs exist so callers that branch-register into them stay
 * self-contained. */
void interrupts_enable(void)
{
}

void interrupts_disable(void)
{
}

/* 0x0A012 - hardware bring-up.  Composed of routines already ported; the
 * only new bits are the three register pokes and the 80-iteration spin,
 * which has no observable effect (it just burns a few hundred cycles while
 * the power/i-clock settles). */
void hw_init(uint8_t *ds)
{
    ds[SFR_0F00A] = 49;   /* arm interrupt sources */
    ds[SFR_0F028] = 23;   /* bus/IO divider */

    /* mov r0, 80 / add r0, 255 / bc ne — a busy wait over 80 decs; time only,
     * so there is no state to model. */

    timer_prescale_setup(ds);
    key_filter_clear(ds);

    ds[SFR_0F03D] = 7;    /* LCD drive level */
    timer_delay(ds, 80);  /* settle delay, counted in timer ticks */

    lcd_hw_setup(ds);
    hw_clear_0F220(ds);
    hw_clear_0F048(ds);

    ds[SFR_0F041] = 0;    /* keyboard region register */
    keyboard_reset(ds);
    key_output_zero(ds);
}

/* 0x0A18A - the one-direction key poll.  er0 starts at 0 and only becomes 1
 * once the row bit has stayed clear for all five samples; zero_KO runs on
 * both exit paths *after* the event word is parked, because the ROM falls
 * straight through the confirm branch into the cleanup. */
uint8_t poll_on_key(uint8_t *ds)
{
    uint8_t tries = 5;

    ds[SFR_KEY_OUTPUT] = 0x10;

    for (;;) {
        if ((ds[SFR_KEY_INPUT] >> 2) & 1) {
            ds[SFR_KEY_OUTPUT] = 0;
            return 0;
        }
        if (--tries == 0)
            break;
        wait_10_ticks(ds);
    }

    ds[RAM_0D150] = 0x04;      /* st er0, [0xD150] with er0 = 0x1004 */
    ds[RAM_0D150 + 1] = 0x10;
    ds[SFR_KEY_OUTPUT] = 0;
    return 1;
}
