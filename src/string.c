/*
 * string.c - string routines from the fx-580VN X firmware.
 *
 * Each function is annotated with its ROM entry point and follows the
 * instruction stream one-for-one, so the C can be checked against the listing
 * line by line.  Address arithmetic is 16-bit, like the machine's.
 */
#include "runtime.h"

/* 0x29A2C */
uint16_t strlen_n(uint8_t *ds, uint16_t s)
{
    uint16_t p = s;
    while (ds[p])
        p++;
    return (uint16_t)(p - s);
}

/* 0x2AB26 - strlen returning a byte (the ROM uses it for short strings) */
uint8_t byte_strlen_n(uint8_t *ds, uint16_t s)
{
    uint8_t n = 0;
    while (ds[s]) {
        s++;
        n++;
    }
    return n;
}

/* 0x0E5C8 - copy including the terminator, returns dst */
uint16_t strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t ret = dst;
    uint8_t c;
    do {
        c = ds[src++];
        ds[dst++] = c;
    } while (c);
    return ret;
}

/* 0x2EDA0 - append src (including its terminator) to dst, returns dst */
uint16_t strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    uint16_t p = dst;
    while (ds[p])
        p++;
    uint8_t c;
    do {
        c = ds[src++];
        ds[p++] = c;
    } while (c);
    return dst;
}

/* 0x203B8 - NULL-safe strlen: len(NULL) == 0 */
uint16_t smart_strlen_n(uint8_t *ds, uint16_t s)
{
    if (s == 0)
        return 0;
    return strlen_n(ds, s);
}

/* 0x203C2 - copy to NULL is a no-op; copy from NULL writes "" */
uint16_t smart_strcpy_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    if (dst == 0)
        return dst;
    if (src == 0) {
        ds[dst] = 0;
        return dst;
    }
    return strcpy_nn(ds, dst, src);
}

/* 0x203D6 - same NULL rules as smart_strcpy_nn */
uint16_t smart_strcat_nn(uint8_t *ds, uint16_t dst, uint16_t src)
{
    if (dst == 0)
        return dst;
    if (src == 0) {
        ds[dst] = 0;
        return dst;
    }
    return strcat_nn(ds, dst, src);
}

/*
 * 0x24BD6 - pointer to string constant #index.
 *
 * 0x0D12B selects the string bank (language): the table at 0x07AC2 holds one
 * base per bank, and each base is the address of a table of string pointers.
 * The pointer tables are 1-based, so entry n is at base + 2*n - 2.
 */
uint16_t get_string_constant(uint8_t *ds, uint16_t index)
{
    uint16_t base = (uint16_t)(0x07AC2u + 2 * ds[0x0D12B]);
    uint16_t table = (uint16_t)(ds[base] | ((uint16_t)ds[(uint16_t)(base + 1)] << 8));
    uint16_t entry = (uint16_t)(table + (uint16_t)(2 * index) - 2);

    return (uint16_t)(ds[entry] | ((uint16_t)ds[(uint16_t)(entry + 1)] << 8));
}

/*
 * 0x2AC9E - expand string constant #index into dst and return its length.
 * The constant itself is stored in the same nested format, so one call only
 * unwraps a single level; str_decompress_print drives it.
 */
uint16_t copy_string_constant(uint8_t *ds, uint16_t index, uint16_t dst)
{
    smart_strcpy_nn(ds, dst, get_string_constant(ds, index));
    return smart_strlen_n(ds, dst);
}
