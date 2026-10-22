/* t_mem.c - differential tests for the block memory cluster. */
#include "harness.h"

#include "../src/runtime.h"

static void test_memcpy(void)
{
    static const uint16_t ns[] = {0, 1, 2, 3, 7, 16, 31, 64, 200, 512};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (int overlap = 0; overlap < 3; overlap++) {
            random_image((uint32_t)(t * 3 + overlap + 500));
            uint16_t dst = 0xD200, src = 0xD600;
            if (overlap == 1) { dst = 0xD200; src = 0xD210; }
            if (overlap == 2) { dst = 0xD210; src = 0xD200; }
            uint16_t n = ns[t];
            Args a = {.er = {dst, src}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x26FFA, &a, "memcpy_nn");
            begin_c();
            uint16_t c = memcpy_nn(after_c, dst, src, n);
            check_ret("memcpy_nn", r, c);
            check_mem("memcpy_nn", RAM_BASE, CMP_END);
        }
    }
}

static void test_memmove(void)
{
    static const uint16_t ns[] = {0, 1, 2, 3, 7, 16, 31, 64, 200};
    static const int offs[][2] = {
        {0, 0}, {0x200, 0x200}, {0x200, 0x210}, {0x210, 0x200},
        {0x200, 0x208}, {0x208, 0x200}, {0x200, 0x600}, {0x600, 0x200},
    };
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (size_t k = 0; k < sizeof(offs) / sizeof(offs[0]); k++) {
            random_image((uint32_t)(t * 17 + k + 600));
            uint16_t dst = (uint16_t)(0xD000 + offs[k][0]);
            uint16_t src = (uint16_t)(0xD000 + offs[k][1]);
            uint16_t n = ns[t];
            Args a = {.er = {dst, src}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x0A8AA, &a, "memmove_nn");
            begin_c();
            uint16_t c = memmove_nn(after_c, dst, src, n);
            check_ret("memmove_nn", r, c);
            check_mem("memmove_nn", RAM_BASE, CMP_END);
        }
    }
}

static void test_memset(void)
{
    static const uint16_t ns[] = {0, 1, 2, 5, 16, 64, 300};
    static const uint8_t vals[] = {0x00, 0xFF, 0x5A};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        for (size_t v = 0; v < sizeof(vals) / sizeof(vals[0]); v++) {
            random_image((uint32_t)(t * 3 + v + 700));
            uint16_t dst = 0xD200;
            uint16_t n = ns[t];
            Args a = {.er = {dst, vals[v]}, .stk = {n}, .nstk = 1};
            uint16_t r = run_rom(0x1EB94, &a, "memset_n");
            begin_c();
            uint16_t c = memset_n(after_c, dst, vals[v], n);
            check_ret("memset_n", r, c);
            check_mem("memset_n", RAM_BASE, CMP_END);
        }
    }
}

static void test_memzero(void)
{
    static const uint16_t ns[] = {0, 1, 2, 5, 16, 64, 300};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        random_image((uint32_t)(t + 800));
        uint16_t dst = 0xD200;
        uint16_t n = ns[t];
        Args a = {.er = {dst, n}, .nstk = 0};
        uint16_t r = run_rom(0x09D34, &a, "memzero");
        begin_c();
        uint16_t c = memzero(after_c, dst, n);
        check_ret("memzero", r, c);
        check_mem("memzero", RAM_BASE, CMP_END);
    }
}

/* Counts with the top bit set: the ROM's `cmp 0, n ; bc lt` takes the branch
 * for every non-zero 16-bit count, i.e. the count is effectively unsigned.
 * The stack is moved to 0xD100 so the copy ranges do not overwrite it. */
static void test_count_edges(void)
{
    random_image(900);
    Args a = {.er = {0xD200, 0xD600}, .stk = {0x8000}, .nstk = 1, .sp = 0xD100};
    uint16_t r1 = run_rom(0x26FFA, &a, "memcpy_nn(0x8000)");
    begin_c();
    uint16_t c1 = memcpy_nn(after_c, 0xD200, 0xD600, 0x8000);
    check_ret("memcpy_nn(0x8000)", r1, c1);
    check_mem("memcpy_nn(0x8000)", 0xD200, CMP_END);

    random_image(901);
    Args b = {.er = {0xD200, 0x5A}, .stk = {0xF000}, .nstk = 1, .sp = 0xD100};
    uint16_t r2 = run_rom(0x1EB94, &b, "memset_n(0xF000)");
    begin_c();
    uint16_t c2 = memset_n(after_c, 0xD200, 0x5A, 0xF000);
    check_ret("memset_n(0xF000)", r2, c2);
    check_mem("memset_n(0xF000)", 0xD200, CMP_END);
}

/* assign_var stores two fixed-width halfs; the mode byte at 0xD111 turns the
 * second move on (69 = cmp? / 196 = the matrix-ish dispatch), slots run 0-12
 * with the ROM taking any 8-bit index */
static void test_assign_var(void)
{
    static const uint8_t modes[] = {56, 1, 69, 196};
    static const uint8_t slots[] = {0, 1, 2, 5, 11, 12, 20};

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        for (size_t s = 0; s < sizeof(slots) / sizeof(slots[0]); s++) {
            uint8_t slot = slots[s];

            random_image(700u + m * 20 + s);
            image[0xD111] = modes[m];
            image[0xE010] = (uint8_t)(0x11 ^ slot);     /* values, first half */
            image[0xE01A] = (uint8_t)(0x22 ^ slot);     /* values, second half */
            Args a = {.er = {slot, 0xE010}, .nstk = 0};
            uint16_t r = run_rom(0x09DE6, &a, "assign_var");
            begin_c();
            uint16_t c = assign_var(after_c, slot, 0xE010);
            check_ret("assign_var", r, c);
            check_mem("assign_var", 0xD300, 0xD6E0);
        }
    }
}

/* 0x09D62 parks er0 at 0xD10E; 0x09E6E answers only in mode 193 (and keeps
 * the incoming high byte); 0x09E24 walks the descriptor table 0x38E6 in ROM
 * with the submode/skew/cell rules. */
static void test_var_helpers(void)
{
    static const uint16_t words[] = {0, 1, 0x1234, 0x8000, 0xABCD, 0xFFFF};
    static const uint8_t modes[] = {0, 56, 69, 193, 196, 255};
    static const uint8_t subs[] = {0, 1, 3, 4, 7, 0x80, 0xFF};

    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        random_image(800u + (unsigned)i);
        Args a = {.er = {words[i], 0}, .nstk = 0};
        run_rom(0x09D62, &a, "store_0D10E");
        begin_c();
        store_0D10E(after_c, words[i]);
        check_mem("store_0D10E", 0xD100, 0xD120);
    }

    /* a few different incoming high bytes: the ROM keeps er0[15:8] */
    for (size_t i = 0; i < 4; i++) {
        for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
            uint16_t arg = (uint16_t)((0x3400u + i * 0x100u) | 7u);

            random_image(810u + (unsigned)(i * 10 + m));
            image[0xD111] = modes[m];
            image[0xD123] = (uint8_t)(0x33 + i);
            Args a = {.er = {arg, 0}, .nstk = 0};
            uint16_t rr = run_rom(0x09E6E, &a, "mode193_value");
            begin_c();
            uint16_t cc = mode193_value(after_c, arg);
            check_ret("mode193_value", rr, cc);
            check_mem("mode193_value", 0xD100, 0xD130);
        }
    }

    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        random_image(820u + (unsigned)i);
        image[0xD112] = subs[i];
        image[0xD14F] = (uint8_t)(0x10 + i);      /* non-zero: skew path */
        image[0xD115] = (uint8_t)(3 + i * 7);
        Args a = {.er = {0x9A2Bu, 0}, .nstk = 0};
        uint16_t rr = run_rom(0x09E24, &a, "var_mode_attr skew");
        begin_c();
        check_ret("var_mode_attr skew", (uint16_t)(rr & 0xFF),
                  var_mode_attr(after_c));
        check_mem("var_mode_attr skew", 0xD100, 0xD160);
    }

    /* and with the skew byte at zero: no shift, plain submode - 1 */
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        random_image(830u + (unsigned)i);
        image[0xD112] = subs[i];
        image[0xD14F] = 0;                        /* skew disabled */
        image[0xD115] = (uint8_t)(0x40 + i);
        Args a = {.er = {0x9A2Bu, 0}, .nstk = 0};
        uint16_t rr = run_rom(0x09E24, &a, "var_mode_attr plain");
        begin_c();
        check_ret("var_mode_attr plain", (uint16_t)(rr & 0xFF),
                  var_mode_attr(after_c));
        check_mem("var_mode_attr plain", 0xD100, 0xD160);
    }
}

void test_mem(void)
{
    test_memcpy();
    test_memmove();
    test_memset();
    test_memzero();
    test_count_edges();
    test_assign_var();
    test_var_helpers();
}
