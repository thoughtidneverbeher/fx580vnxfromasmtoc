/*
 * xcheck.c - emit a trace of (result, memory hash) for a fixed set of function
 * calls.  The same script (tools/xcheck.lua) runs in CasioEmu, so the two
 * outputs can be diffed: it validates the C core in emu/nx8.c against the
 * reference emulator on real function calls.
 *
 * Output format, one line per case:
 *   CASE <name> RET <hex> HASH <hex>
 */
#include "../emu/nx8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Nx8Machine m;
static uint8_t rom[0x40000];
static uint8_t image[0x10000];

typedef struct {
    const char *name;
    uint32_t addr;
    uint16_t er[8];
    uint16_t stk[2];
    int nstk;
    void (*setup)(void);
} Case;

static void putstr(uint16_t a, const char *s)
{
    while (*s)
        image[a++] = (uint8_t)*s++;
    image[a] = 0;
}

static void s_hello(void) { putstr(0xD200, "hello"); }
static void s_two(void) { putstr(0xD200, "hello"); putstr(0xD300, "abc"); }
static void s_mem(void)
{
    for (int i = 0; i < 32; i++)
        image[0xD200 + i] = (uint8_t)('A' + i);
}
static void s_empty(void) { image[0xD200] = 0; }
static void s_two_empty(void) { image[0xD200] = 0; putstr(0xD300, "abc"); }
static void s_overlap(void)
{
    for (int i = 0; i < 32; i++)
        image[0xD200 + i] = (uint8_t)(0x40 + i);
}

static const Case cases[] = {
    {"strlen_n", 0x29A2C, {0xD200}, {0}, 0, s_hello},
    {"byte_strlen_n", 0x2AB26, {0xD200}, {0}, 0, s_hello},
    {"strcpy_nn", 0x0E5C8, {0xD300, 0xD200}, {0}, 0, s_two},
    {"strcat_nn", 0x2EDA0, {0xD300, 0xD200}, {0}, 0, s_two},
    {"memcpy_nn", 0x26FFA, {0xD300, 0xD200}, {7, 0}, 1, s_mem},
    {"memmove_fwd", 0x0A8AA, {0xD300, 0xD200}, {8, 0}, 1, s_overlap},
    {"memmove_bwd", 0x0A8AA, {0xD200, 0xD204}, {8, 0}, 1, s_overlap},
    {"memset_n", 0x1EB94, {0xD300, 0xAA}, {9, 0}, 1, s_hello},
    {"memzero", 0x09D34, {0xD300, 5}, {0}, 0, s_hello},
    {"smart_strcpy_null", 0x203C2, {0x0000, 0xD200}, {0}, 0, s_hello},
    {"smart_strcpy_empty", 0x203C2, {0xD300, 0x0000}, {0}, 0, s_two},
    {"smart_strcat_null", 0x203D6, {0xD300, 0x0000}, {0}, 0, s_two},
    {"smart_strcat_dstnull", 0x203D6, {0x0000, 0xD200}, {0}, 0, s_two},
    {"smart_strlen_null", 0x203B8, {0x0000}, {0}, 0, s_hello},
    {"strlen_empty", 0x29A2C, {0xD200}, {0}, 0, s_empty},
    {"strcat_empty", 0x2EDA0, {0xD300, 0xD200}, {0}, 0, s_two_empty},
    {"memmove_same", 0x0A8AA, {0xD200, 0xD200}, {8, 0}, 1, s_overlap},
    {"memcpy_zero", 0x26FFA, {0xD300, 0xD200}, {0, 0}, 1, s_mem},
};

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "resource/rom.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
        fprintf(stderr, "short rom\n");
        return 1;
    }
    fclose(f);

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const Case *cs = &cases[c];
        memset(image, 0, sizeof(image));
        for (uint32_t a = 0xD000; a < 0xF000; a++)
            image[a] = (uint8_t)((a * 37 + 11) & 0xFF);
        cs->setup();

        nx8_init(&m);
        nx8_load_rom(&m, rom, sizeof(rom));
        memcpy(m.mem + 0xD000, image + 0xD000, 0x3000);
        for (int i = 0; i < 8; i++)
            nx8_set_er(&m, (unsigned)(i * 2), cs->er[i]);
        m.sp = 0xED00;
        for (int i = 0; i < cs->nstk; i++) {
            m.mem[m.sp + 2 * i] = (uint8_t)(cs->stk[i] & 0xFF);
            m.mem[m.sp + 2 * i + 1] = (uint8_t)(cs->stk[i] >> 8);
        }
        m.psw = 0;
        m.dsr = 0;
        size_t steps = nx8_call(&m, cs->addr, 8000000);
        if (steps == (size_t)-1) {
            printf("CASE %s FAULT %s\n", cs->name, m.fault_msg ? m.fault_msg : "?");
            continue;
        }
        printf("CASE %s RET %04X HASH %08X\n", cs->name, nx8_er(&m, 0),
               fnv(m.mem + 0xD000, 0x1C00));
    }
    return 0;
}
