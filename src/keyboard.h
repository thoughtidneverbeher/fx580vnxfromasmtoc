/* keyboard.h - keyboard scanning and the timer that debounces it.
 *
 * The calculator scans the keyboard by driving one column (a byte written to
 * 0xF046) and reading the key rows back from 0xF040: a pressed key shows up
 * as a clear bit.  The debounce wait uses the built-in timer at 0xF020.
 */
#ifndef NX_KEYBOARD_H
#define NX_KEYBOARD_H

#include <stdint.h>

#define SFR_KEY_INPUT      0xF040u   /* keyboard.ki   - key rows */
#define SFR_KEY_IN_FILTER  0xF042u   /* keyboard.ki_filter */
#define SFR_KEY_OUT_MASK   0xF044u   /* keyboard.ko_mask */
#define SFR_KEY_OUTPUT     0xF046u   /* keyboard.ko   - driven column */

#define SFR_TIMER_INTERVAL 0xF020u   /* timer.interval */
#define SFR_TIMER_COUNTER  0xF022u   /* timer.counter  */
#define SFR_TIMER_PRESCALE 0xF024u
#define SFR_TIMER_CONTROL  0xF025u

#define SFR_0F008 0xF008u
#define SFR_0F009 0xF009u
#define SFR_0F00A 0xF00Au
#define SFR_0F014 0xF014u
#define SFR_0F015 0xF015u
#define SFR_0F028 0xF028u
#define SFR_0F03D 0xF03Du
#define SFR_0F041 0xF041u
#define RAM_0D13A 0xD13Au
#define RAM_0D150 0xD150u

/* 0x09F3A - program the timer for a wait of `interval` ticks. */
void timer_delay(uint8_t *ds, uint16_t interval);

/* 0x09FF6 - end of a key scan: arms the next one. */
void key_scan_end(uint8_t *ds);

/* 0x0E826 - is any key of one column pressed?  `desc` points at a two-byte
 * descriptor in the data space: [0] = the row mask, [1] = the column byte
 * to drive.  Returns 1 when a key is pressed (debounced), 0 otherwise. */
uint8_t check_any_key_pressed(uint8_t *ds, uint16_t desc);

/* 0x0A080 - timer prescaler setup: 0xF010 = 34 and 0xF018 = 3.  The copy of
 * the byte at 0xF012 comes from r1, but the `mov er0, 34` just before it
 * clears that register, so it is always zero. */
void timer_prescale_setup(uint8_t *ds);

/* 0x0A15A - arm the key input filter. */
void key_filter_clear(uint8_t *ds);

/* 0x0A162 / 0x0A16A - drive the column output with all bits / nothing. */
void key_output_all(uint8_t *ds);
void key_output_zero(uint8_t *ds);

/* 0x0A172 - release test: drive all columns and report whether the rows read
 * back as all ones, i.e. whether no key is held down. */
uint8_t key_all_released(uint8_t *ds);

/* 0x09A2A - keyboard reset. */
void keyboard_reset(uint8_t *ds);

/* 0x0A060 / 0x0A070 - clear two SFR blocks the firmware keeps for the
 * keyboard/LCD controller. */
void hw_clear_0F220(uint8_t *ds);
void hw_clear_0F048(uint8_t *ds);

/* 0x0A0AA - LCD controller setup. */
void lcd_hw_setup(uint8_t *ds);

/* 0x29892 - convert a raw KI/KO keycode pair into a logical keycode.
 *   key_storage  points at the two bytes the scanner left there: [0] = the
 *                column (KO) bit, [1] = the row (KI) bit
 *   table        base of the keycode table in the ROM (0x7B0E for normal
 *                keys, 0x83DA for the Base-N layer)
 * A byte that is zero (no key in that direction) yields 0.  Otherwise the
 * table index is (column bit << 3) | row bit, each counted as the number of
 * shifts the byte needs to reach zero, minus one. */
uint16_t keycode_cvt(uint8_t *ds, uint16_t key_storage, uint16_t table);

/* 0x23CA4 - wait 10 timer ticks.  The original shields its caller's er0 with
 * a push/pop xr0 pair, so it may be dropped between register-changing code. */
void wait_10_ticks(uint8_t *ds);

/* 0x0A058 / 0x0A05C - two-instruction interrupt gate stubs (`ei; rt` /
 * `di; rt`).  They touch no data-space state; the C model keeps only the
 * convention that the ROM may start catching timer interrupts after use. */
void interrupts_enable(void);
void interrupts_disable(void);

/* 0x0A012 - boot-time hardware bring-up: arm the interrupt source at
 * 0xF00A, set the bus divider at 0xF028, spin 80 iterations so the rails
 * settle, then run the timer prescaler, key filter, an 80-tick «delay», the
 * LCD setup, the two SFR block clears and finally the keyboard reset with
 * the column outputs driven low.  0xF03D (drive level 7) and 0xF041 (0) are
 * written on the way through. */
void hw_init(uint8_t *ds);

/* 0x0A18A - held-key watchdog for one matrix direction: drive column 0x10,
 * then sample KI bit 2 five times with a 10-tick pause between samples.
 * The bit staying clear the whole time (a pressed key pulls a row bit low)
 * confirms a held key: the event word 0x1004 is parked at 0xD150 and 1 is
 * returned.  A set bit at any sample bails out early with 0.  Either way
 * the column output ends at 0. */
uint8_t poll_on_key(uint8_t *ds);

#endif
