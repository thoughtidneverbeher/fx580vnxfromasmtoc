/* t_print.c - differential tests for screen blending and character drawing. */
#include "harness.h"

#include <stdio.h>
#include <string.h>

#include "../src/print.h"

static uint32_t case_rng;

static uint32_t next_rnd(void)
{
    case_rng = case_rng * 1103515245u + 12345u;
    return case_rng >> 8;
}

static void test_screen_blend(void)
{
    static const uint16_t addrs[] = {
        0xDDD4, 0xDDEA, 0xDDEB, 0xDDEC, 0xE3D4, 0xE9D3, 0xE9D4, 0xF800, 0xFF00,
    };
    static const uint8_t modes[] = {0, 1, 2, 3, 4, 5, 255};
    static const uint8_t masks[] = {0x00, 0xFF, 0x80, 0x7F, 0x0F, 0xF0, 0x01};
    static const uint8_t values[] = {0x00, 0xFF, 0xA5, 0x5A};

    for (size_t a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
        for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
            for (size_t k = 0; k < sizeof(masks) / sizeof(masks[0]); k++) {
                for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
                    random_image((uint32_t)(a * 500 + m * 50 + k * 7 + v + 9000));
                    image[0xD138] = modes[m];
                    image[0xD120] = values[v];
                    Args args = {
                        .er = {addrs[a], 0xD120},
                        .stk = {masks[k]},
                        .nstk = 1,
                    };
                    run_rom(0x093D2, &args, "screen_blend");
                    begin_c();
                    screen_blend(after_c, addrs[a], values[v], masks[k]);
                    check_mem("screen_blend", 0xD000, 0xE9D4);
                }
            }
        }
    }
}

static void check_addr(uint32_t impl, const char *what, uint16_t xy, uint8_t cur)
{
    random_image((uint32_t)(impl * 31 + xy + cur + 12000));
    image[0xD139] = cur;
    Args args = {.er = {xy, 0xD200}};
    uint16_t rom_ret = run_rom(impl, &args, what);
    begin_c();

    uint16_t addr = 0;
    uint16_t bit = impl == 0x094BE ? lcd_addr(after_c, xy, &addr)
                                   : screen_addr(after_c, xy, &addr);
    uint16_t rom_addr = (uint16_t)(after_rom[0xD200] |
                                   (after_rom[0xD201] << 8));

    /* The ROM leaves the y coordinate in the high byte of er0; callers only
     * use the low byte, which holds the bit position. */
    check_ret(what, (uint16_t)(rom_ret & 0xFF), bit);
    checks++;
    if (rom_addr != addr) {
        failures++;
        printf("FAIL %-24s addr: rom=%04X c=%04X\n", what, rom_addr, addr);
    }
}

static void test_screen_addr(void)
{
    static const uint16_t xys[] = {
        0x0000, 0x0001, 0x0007, 0x0008, 0x00BF, 0x00C0,
        0x0100, 0x0519, 0x3FBF, 0x4000, 0x8000, 0x8100, 0xFF00, 0xFF01,
    };

    for (size_t i = 0; i < sizeof(xys) / sizeof(xys[0]); i++) {
        for (int cur = 0; cur < 2; cur++) {
            check_addr(0x094C8, "screen_addr", xys[i], (uint8_t)cur);
            check_addr(0x094BE, "lcd_addr", xys[i], (uint8_t)cur);
        }
    }
}

static void one_print(uint32_t seed, uint16_t xy, uint8_t ch, uint8_t marker,
                      uint8_t font, uint8_t cur, uint8_t mode)
{
    random_image(seed);
    image[0xD137] = font;
    image[0xD138] = mode;
    image[0xD139] = cur;

    Args args = {.er = {xy, (uint16_t)(((uint16_t)marker << 8) | ch)}};
    run_rom(0x09056, &args, "char_print");
    begin_c();
    char_print(after_c, xy, ch, marker);
    check_mem("char_print", 0xDDD4, 0xE9D4);
}

static void test_char_print(void)
{
    static const uint8_t markers[] = {0, 1, 2, 15, 16, 240, 241, 242, 255};
    static const uint8_t fonts[] = {14, 10, 8};
    static const uint8_t chars[] = {16, 17, 32, 48, 65, 91, 124, 200, 255};
    static const uint8_t xs[] = {0, 1, 5, 7, 8, 11, 15, 191, 192, 255};
    static const uint8_t ys[] = {0, 1, 5, 40, 62, 63, 64, 128, 200, 255};

    case_rng = 12345;

    /* Deterministic edge sweep. */
    for (size_t f = 0; f < 3; f++) {
        for (size_t m = 0; m < 9; m++) {
            for (size_t x = 0; x < sizeof(xs); x++) {
                for (size_t y = 0; y < sizeof(ys); y++) {
                    uint32_t seed = (uint32_t)(f * 100000 + m * 10000 +
                                               x * 100 + y * 7 + 77);
                    one_print(seed, (uint16_t)((ys[y] << 8) | xs[x]),
                              chars[(x + y + m) % 9], markers[m], fonts[f],
                              (uint8_t)((x + y) & 1), (uint8_t)((x + y + m) % 6));
                }
            }
        }
    }

    /* Randomised sweep over the full parameter space. */
    for (int i = 0; i < 2500; i++) {
        uint8_t font = fonts[next_rnd() % 3];
        uint8_t marker = markers[next_rnd() % 9];
        uint8_t ch = (uint8_t)next_rnd();
        uint8_t x = (uint8_t)(next_rnd() % 200);
        uint8_t y = (uint8_t)(next_rnd() % 68);
        uint8_t cur = (uint8_t)(next_rnd() & 1);
        uint8_t mode = (uint8_t)(next_rnd() % 6);

        one_print(0x50000u + (uint32_t)i, (uint16_t)((y << 8) | x),
                  ch, marker, font, cur, mode);
    }
}

static void one_line(uint32_t seed, uint16_t xy, const uint8_t *chars, size_t n,
                     uint8_t font, uint8_t cur, uint8_t mode, uint8_t flag, uint8_t calc_mode)
{
    random_image(seed);
    memset(image + 0xD400, 0, 0x100);
    for (size_t i = 0; i < n; i++)
        image[0xD400 + i] = chars[i];
    image[0xD137] = font;
    image[0xD138] = mode;
    image[0xD139] = cur;
    image[0xD11C] = flag;
    image[0xD111] = calc_mode;

    Args args = {.er = {xy, 0xD400}};
    uint16_t rom_ret = run_rom(0x08F7E, &args, "line_print");
    uint16_t rom_str = nx8_er(harness_machine(), 2);
    begin_c();

    uint16_t str_out = 0;
    uint8_t count = line_print(after_c, xy, 0xD400, &str_out);

    checks++;
    if ((rom_ret & 0xFF) != (uint16_t)count || rom_str != str_out) {
        failures++;
        printf("FAIL %-24s count: rom=%02X c=%02X, str: rom=%04X c=%04X\n",
               "line_print", rom_ret & 0xFF, count, rom_str, str_out);
    }
    check_mem("line_print", 0xDDD4, 0xE9D4);
}

/* A short alphabet that exercises every branch of the loop. */
static uint8_t line_pool(uint32_t i)
{
    static const uint8_t pool[] = {
        0, 1, 4, 5, 16, 24, 32, 48, 65, 91, 124, 127, 200, 217,
        238, 239, 240, 241, 242, 243, 250,
    };
    return pool[i % sizeof(pool) / sizeof(pool[0])];
}

static void test_line_print(void)
{
    /* Empty and marker-only strings, plus strings that do not fit on a line. */
    static const uint8_t empty[] = {0};
    static const uint8_t markers[] = {'\0'};
    static const uint8_t marker_run[] = {240, 241, 242, 243, 250, 0};
    static const uint8_t escape[] = {4, 'A', 'B', 4, 'C', 0};
    static const uint8_t ctrl[] = {1, 1, 'A', 1, 0};
    static const uint8_t long_str[] = {
        48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
        63, 64, 65, 66, 67, 68, 69, 0
    };
    struct {
        const uint8_t *s;
        size_t n;
    } fixed[] = {
        {empty, 1}, {markers, 1}, {marker_run, 6},
        {escape, 6}, {ctrl, 5}, {long_str, 23},
    };
    static const uint16_t xs[] = {0, 1, 3, 5, 180, 181, 182, 185, 186, 187, 191, 200};
    static const uint16_t ys[] = {0, 1, 40, 63, 64, 200, 255};

    for (size_t f = 0; f < sizeof(fixed) / sizeof(fixed[0]); f++) {
        for (uint8_t font = 0; font < 3; font++) {
            for (size_t xi = 0; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
                for (size_t yi = 0; yi < sizeof(ys) / sizeof(ys[0]); yi++) {
                    one_line(70000u + f * 1000 + font * 100 + xi * 10 + yi,
                             (uint16_t)((ys[yi] << 8) | xs[xi]),
                             fixed[f].s, fixed[f].n,
                             (uint8_t)(font == 0 ? 14 : (font == 1 ? 10 : 8)),
                             (uint8_t)((xi + yi) & 1),
                             (uint8_t)((xi + yi) % 6),
                             (uint8_t)(xi & 1),
                             (uint8_t)(yi % 3));
                }
            }
        }
    }

    /* Randomised strings and start positions. */
    for (int i = 0; i < 600; i++) {
        uint8_t buf[26];
        size_t n = 0;
        uint8_t x, y, fonts[3] = {14, 10, 8};

        while (n < 24) {
            buf[n] = line_pool(next_rnd());
            if (buf[n] == 0)
                break;
            n++;
        }
        buf[n] = 0;
        x = (uint8_t)(next_rnd() % 200);
        y = (uint8_t)(next_rnd() % 70);
        one_line(79000u + (uint32_t)i, (uint16_t)((y << 8) | x),
                 buf, n + 1, fonts[next_rnd() % 3],
                 (uint8_t)(next_rnd() & 1), (uint8_t)(next_rnd() % 6),
                 (uint8_t)(next_rnd() & 1), (uint8_t)(next_rnd() % 3));
    }
}

static void test_get_string_constant(void)
{
    static const uint16_t banks[] = {0, 1, 2, 3, 4, 15, 255};
    static const uint16_t index[] = {0, 1, 2, 3, 7, 12, 33, 100, 300, 1000};

    for (size_t b = 0; b < sizeof(banks) / sizeof(banks[0]); b++) {
        for (size_t i = 0; i < sizeof(index) / sizeof(index[0]); i++) {
            random_image(0x90000u + b * 100 + i);
            image[0x0D12B] = (uint8_t)banks[b];
            Args args = {.er = {index[i], 0}};
            uint16_t r = run_rom(0x24BD6, &args, "get_string_constant");
            begin_c();
            uint16_t c = get_string_constant(after_c, index[i]);
            check_ret("get_string_constant", r, c);
        }
    }
}

static void test_copy_string_constant(void)
{
    static const uint16_t index[] = {0, 1, 4, 6, 8, 12, 33, 100};

    for (int bank = 0; bank < 3; bank++) {
        for (size_t i = 0; i < sizeof(index) / sizeof(index[0]); i++) {
            for (uint16_t dst = 0xD400; dst < 0xD500; dst += 0x37) {
                random_image(0x91000u + bank * 100 + i * 10 + dst / 0x37);
                image[0x0D12B] = (uint8_t)bank;
                /* a few bytes of junk ahead of the destination */
                for (int k = 0; k < 3; k++)
                    image[(uint16_t)(dst - 3 + k)] = (uint8_t)(k * 40 + 7);
                Args args = {.er = {index[i], dst}};
                uint16_t r = run_rom(0x2AC9E, &args, "copy_string_constant");
                begin_c();
                uint16_t c = copy_string_constant(after_c, index[i], dst);
                check_ret("copy_string_constant", r, c);
                check_mem("copy_string_constant", 0xD000, 0xD500);
            }
        }
    }
}

static void test_decompress_print(void)
{
    /* Nested smart strings: plain bytes, tag 2 (index as-is) and tag 4
     * (index = byte - 1), so both reference forms are covered. */
    struct {
        const uint8_t *s;
        size_t n;
        uint16_t font;
    } cases[] = {
        {0, 0, 14},
        {(const uint8_t *)"\0", 1, 14},
        {(const uint8_t *)"AB\0", 3, 14},
        {(const uint8_t *)"\4\7C\0", 4, 14},
        {(const uint8_t *)"\2\6C\0", 4, 14},
        {(const uint8_t *)"XY\4\10\4\12Z\0", 8, 14},
        {(const uint8_t *)"\4\7\2\6\0", 5, 10},
        {(const uint8_t *)"\4\7\2\6\0", 5, 8},
        {(const uint8_t *)"\377\0", 2, 14},
        {(const uint8_t *)"AB\377C\0", 5, 14},
        {(const uint8_t *)"\4\7\2\6D\0", 6, 14},
    };
    /* Only the two banks the firmware actually uses (English and Vietnamese);
     * the third table points at unrelated ROM data whose expansions overflow
     * the ROM's 64-byte frame and corrupt its return address. */
    static const uint8_t banks[] = {0, 1};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (size_t b = 0; b < sizeof(banks) / sizeof(banks[0]); b++) {
            random_image(0x92000u + i * 10 + b);
            if (cases[i].n)
                memcpy(image + 0xD200, cases[i].s, cases[i].n);
            image[0xD200 + cases[i].n] = 0;
            image[0xD137] = cases[i].font;
            image[0xD138] = (uint8_t)(i % 6);
            image[0xD139] = (uint8_t)((i + b) & 1);
            image[0x0D12B] = banks[b];

            Args args = {.er = {0, 0xD200}};
            uint16_t rom_ret = run_rom(0x08F02, &args, "str_decompress_print");
            uint16_t rom_str = nx8_er(harness_machine(), 2);
            begin_c();

            uint16_t end = 0;
            uint16_t c = str_decompress_print(after_c, 0xD200, DECOMP_BUFFER, &end);
            check_ret("str_decompress_print", (uint16_t)(rom_ret & 0xFF),
                      (uint16_t)(c & 0xFF));
            checks++;
            if (rom_str != end) {
                failures++;
                printf("FAIL %-24s end: rom=%04X c=%04X\n",
                       "str_decompress_print", rom_str, end);
            }
            check_mem("str_decompress_print globals", 0xD000, 0xDDD4);
            check_mem("str_decompress_print screen", 0xDDD4, 0xE9D4);
            check_mem("str_decompress_print frame", DECOMP_BUFFER,
                      (uint16_t)(DECOMP_BUFFER + 0x40));
        }
    }
}

void test_print(void)
{
    test_screen_blend();
    test_screen_addr();
    test_char_print();
    test_line_print();
    test_get_string_constant();
    test_copy_string_constant();
    test_decompress_print();
}
