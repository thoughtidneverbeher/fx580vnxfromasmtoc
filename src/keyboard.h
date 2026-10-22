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
#define RAM_0D13A 0xD13Au

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

#endif
