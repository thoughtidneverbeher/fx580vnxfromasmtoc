/* t_font.c - differential tests for glyph extraction. */
#include "harness.h"

#include "../src/font.h"

static void test_char_get_14(void)
{
    static const uint8_t markers[] = {0, 1, 2, 15, 16, 240, 241, 242, 255};
    static const uint8_t fonts[] = {14, 10, 8};
    for (size_t m = 0; m < sizeof(markers) / sizeof(markers[0]); m++) {
        for (size_t f = 0; f < sizeof(fonts) / sizeof(fonts[0]); f++) {
            for (int c = 0; c < 256; c++) {
                random_image((uint32_t)(m * 300 + f * 100 + c + 2000));
                image[0xD137] = fonts[f];
                uint16_t out = 0xD200;
                Args a = {.er = {(uint16_t)((markers[m] << 8) | c), out}, .nstk = 0};
                uint16_t r = run_rom(0x09238, &a, "char_get_14");
                begin_c();
                uint16_t cret = char_get_14(after_c, (uint8_t)c, markers[m], out);
                check_ret("char_get_14", r, cret);
                check_mem("char_get_14", 0xD200, 0xD240);
            }
        }
    }
}

static void test_char_get_l14(void)
{
    static const uint8_t markers[] = {0, 1, 2, 15, 16, 240, 241, 242, 255};
    static const uint8_t fonts[] = {14, 10, 8};
    for (size_t m = 0; m < sizeof(markers) / sizeof(markers[0]); m++) {
        for (size_t f = 0; f < sizeof(fonts) / sizeof(fonts[0]); f++) {
            for (int c = 0; c < 256; c++) {
                random_image((uint32_t)(m * 300 + f * 100 + c + 3000));
                image[0xD137] = fonts[f];
                uint16_t out = 0xD200;
                Args a = {.er = {(uint16_t)((markers[m] << 8) | c), out}, .nstk = 0};
                uint16_t r = run_rom(0x09318, &a, "char_get_l14");
                begin_c();
                uint16_t cret = char_get_l14(after_c, (uint8_t)c, markers[m], out);
                check_ret("char_get_l14", r, cret);
                check_mem("char_get_l14", 0xD200, 0xD240);
            }
        }
    }
}

void test_font(void)
{
    test_char_get_14();
    test_char_get_l14();
}
