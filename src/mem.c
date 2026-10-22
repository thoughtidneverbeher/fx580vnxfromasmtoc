/*
 * mem.c - block memory routines from the fx-580VN X firmware.
 *
 * The count is a 16-bit value passed on the stack.  The ROM's loops are
 * count-first (`while (n > 0) { ...; n--; }`) and use `cmp 0, n ; bc lt`,
 * which for every non-zero 16-bit count takes the branch, i.e. the count is
 * effectively unsigned.
 */
#include "runtime.h"

/* 0x26FFA */
uint16_t memcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n)
{
    uint16_t d = dst;
    uint16_t s = src;
    while (n > 0) {
        ds[d++] = ds[s++];
        n--;
    }
    return dst;
}

/* 0x0A8AA - overlap-safe copy; the ROM picks the direction from the signed
 * 16-bit ordering of the pointers and of src + n. */
uint16_t memmove_nn(uint8_t *ds, uint16_t dst, uint16_t src, uint16_t n)
{
    uint16_t d = dst;
    uint16_t s = src;

    if ((int16_t)src >= (int16_t)dst || (int16_t)dst >= (int16_t)(uint16_t)(src + n)) {
        /* forward */
        while (n > 0) {
            ds[d++] = ds[s++];
            n--;
        }
    } else {
        /* backward */
        d += n;
        s += n;
        while (n > 0) {
            d--;
            s--;
            ds[d] = ds[s];
            n--;
        }
    }
    return dst;
}

/* 0x1EB94 */
uint16_t memset_n(uint8_t *ds, uint16_t dst, uint8_t value, uint16_t n)
{
    uint16_t d = dst;
    while (n > 0) {
        ds[d++] = value;
        n--;
    }
    return dst;
}

/* 0x09D34 - memset(dst, 0, n) */
uint16_t memzero(uint8_t *ds, uint16_t dst, uint16_t n)
{
    return memset_n(ds, dst, 0, n);
}

#define MODE_ADDR 0xD111   /* mode byte: <58 normal, 68/69 comp?, 196 ... */

/* the little state bytes the cluster keeps beside the mode byte */
#define SUBMODE_ADDR  0xD112  /* submode byte, one past the mode byte */
#define CELL_SEL_ADDR 0xD115  /* cell offset byte inside a descriptor row */
#define SKEW_ADDR     0xD14F  /* non-zero: apply the +4 row skew (0x09E24) */
#define KEEP_ADDR     0xD10E  /* 16-bit word the runtime parks between calls */
#define MODE193_ADDR  0xD123  /* the value mode 193 reports through 0x09E6E */

/* 0x09DE6 - assign_var.  Store the ten-byte variable value at `src` into
 * the variable slot area: slot `slot` (the r0 argument) lives at
 * 0xD31A + 10*slot.  In the matrix/vector modes (mode byte 69 or 196 at
 * 0xD111) a second ten-byte half is stored beside the first, from src+10
 * into 0xD612 + 10*slot - the double-width slots those modes keep.
 *
 * The return value is the ROM's ABI accident, kept as-is: the byte counts
 * the caller passes to _memcpy_nn ride on the stack, and the lib burns them
 * down to zero in place, so the trailing `pop er0` in the widened path
 * yields 0.  In the basic path the last value loaded into r0 is the byte
 * at 0xD111, and er0 becomes just that: no copy happened, the "result" is
 * the mode byte itself. */
uint16_t assign_var(uint8_t *ds, uint8_t slot, uint16_t src)
{
    uint16_t off = (uint16_t)(10u * slot);

    memcpy_nn(ds, (uint16_t)(0xD31A + off), src, 10);

    uint8_t mode = ds[MODE_ADDR];
    if (mode == 69 || mode == 196) {
        memcpy_nn(ds, (uint16_t)(0xD612 + off), (uint16_t)(src + 10), 10);
        return 0;
    }
    return mode;
}

/* 0x09D62 - park a 16-bit word at 0xD10E.  Two-instruction leaf: the store
 * uses er0's register pair, so ds[0xD10E] takes the low byte and ds[0xD10F]
 * the high one, and er0 leaves untouched. */
void store_0D10E(uint8_t *ds, uint16_t v)
{
    ds[KEEP_ADDR] = (uint8_t)v;
    ds[KEEP_ADDR + 1] = (uint8_t)(v >> 8);
}

/* 0x09E6E - one-byte attribute of the active mode.  Only mode 193 carries a
 * payload (the byte at 0xD123); every other mode reports 0.  The ROM leaves
 * er0's high byte (the incoming r1) untouched, so the C model keeps the
 * argument pair and mirrors that. */
uint16_t mode193_value(uint8_t *ds, uint16_t arg)
{
    uint16_t hi = (uint16_t)(arg & 0xFF00u);

    if (ds[MODE_ADDR] != 193)
        return hi;                          /* mov r0, 0 */
    return (uint16_t)(hi | ds[MODE193_ADDR]);
}

/* 0x09E24 - one byte of the per-submode descriptor table at 0x38E6 in ROM.
 * The submode byte at 0xD112 picks the row (5 bytes each, rows counted from
 * one); in submodes of 4 and up with 0xD14F non-zero the row count shifts
 * by 4 (the skew the alternate layers use), and the cell inside the row is
 * the byte at 0xD115.  The routine ignores its incoming registers, and it
 * zeroes r1 itself before the load, so the result is a clean byte. */
uint8_t var_mode_attr(uint8_t *ds)
{
    uint8_t row = ds[SUBMODE_ADDR];

    if (row >= 4 && ds[SKEW_ADDR] != 0)
        row = (uint8_t)(row + 4);
    row = (uint8_t)(row - 1);

    uint16_t cell = (uint16_t)(5u * row);
    cell = (uint16_t)(cell + 0x38E6u);
    cell = (uint16_t)(cell + ds[CELL_SEL_ADDR]);
    return ds[cell];
}
