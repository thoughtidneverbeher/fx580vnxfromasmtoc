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
