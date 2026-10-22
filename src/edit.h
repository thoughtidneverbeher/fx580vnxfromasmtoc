/* edit.h - pieces of the text-entry box that shows the expression.
 *
 * The editor region sits around 0x0AC30-0x0C000 in the ROM.  Its text buffer
 * is an array of two-byte units at 16-bit offsets in the data space.
 */
#ifndef NX_EDIT_H
#define NX_EDIT_H

#include <stdint.h>

/* 0x0AC60 - step one display unit backwards.  `lo` bounds the region,
 * `probe` is the position the caller walks back from.  The result is:
 *   - 0 when `lo` is at or past `probe` (nothing left in the region),
 *   - `probe - 1` when `probe` sits directly after `lo`,
 *   - otherwise the byte before `probe` decides the unit width: a byte of
 *     0xF0..0xFF (the marker/tag range) means the unit is two bytes and the
 *     answer is `probe - 2`; any other byte means a one-byte unit and the
 *     answer is `probe - 1`.
 * The region compare is the ROM's unsigned `bc ge` test.
 */
uint16_t prev_unit_off(uint8_t *ds, uint16_t lo, uint16_t probe);

#endif /* NX_EDIT_H */
