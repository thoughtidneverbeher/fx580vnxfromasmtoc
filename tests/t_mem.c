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

void test_mem(void)
{
    test_memcpy();
    test_memmove();
    test_memset();
    test_memzero();
    test_count_edges();
}
