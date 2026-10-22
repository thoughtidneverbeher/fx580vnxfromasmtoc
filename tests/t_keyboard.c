/* t_keyboard.c - differential tests for keyboard scanning and the timer. */
#include "harness.h"

#include "../src/keyboard.h"

static void test_timer_delay(void)
{
    static const uint16_t intervals[] = {0, 1, 10, 100, 0x07FF, 0xFFFF};
    static const uint8_t filter[] = {0, 1, 0xFF};
    static const uint8_t flag[] = {0, 2, 0xFF};

    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        for (size_t f = 0; f < sizeof(filter) / sizeof(filter[0]); f++) {
            for (size_t g = 0; g < sizeof(flag) / sizeof(flag[0]); g++) {
                random_image(0xA0000u + i * 100 + f * 10 + g);
                /* the SFR window is part of the image; only the bits the
                 * function depends on are forced */
                image[0xF00A] &= (uint8_t)~0x02;
                image[0xF00A] |= (uint8_t)(flag[g] & 0x02);
                image[0xF042] = filter[f];
                Args args = {.er = {intervals[i], 0}};
                run_rom(0x09F3A, &args, "timer_delay");
                begin_c();
                timer_delay(after_c, intervals[i]);
                check_mem("timer_delay", 0xF000, 0xF060);
            }
        }
    }
}

static void test_check_any_key(void)
{
    static const uint8_t masks[] = {0x00, 0x01, 0x7F, 0x80, 0xF0, 0xFF};
    static const uint8_t columns[] = {0x00, 0x01, 0x55, 0xFF};
    static const uint8_t rows[] = {0x00, 0x01, 0x0F, 0xF0, 0xFF};
    static const uint8_t filters[] = {0, 1};

    for (size_t m = 0; m < sizeof(masks) / sizeof(masks[0]); m++) {
        for (size_t c = 0; c < sizeof(columns) / sizeof(columns[0]); c++) {
            for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
                for (size_t f = 0; f < sizeof(filters) / sizeof(filters[0]); f++) {
                    random_image(0xB0000u + m * 1000 + c * 100 + r * 10 + f);
                    /* 2-byte key descriptor: row mask, column byte */
                    image[0xE300] = masks[m];
                    image[0xE301] = columns[c];
                    image[0xF040] = rows[r];
                    image[0xF042] = filters[f];
                    Args args = {.er = {0xE300, 0}};
                    uint16_t rc = run_rom(0x0E826, &args, "check_any_key_pressed");
                    begin_c();
                    uint8_t cc = check_any_key_pressed(after_c, 0xE300);
                    check_ret("check_any_key_pressed", (uint16_t)(rc & 0xFF), cc);
                    check_mem("check_any_key_pressed", 0xF000, 0xF060);
                    check_mem("check_any_key_pressed ram", 0xD100, 0xD140);
                }
            }
        }
    }
}

static void test_key_helpers(void)
{
    static const uint8_t values[] = {0, 1, 0x7F, 0x80, 0xFF};

    for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
        uint16_t expected;

        random_image(0xC0000u + v * 3);
        /* the helper reads r1 (the high byte of er0), but clears it first */
        Args args = {.er = {(uint16_t)(values[v] << 8), 0}};
        run_rom(0x0A080, &args, "timer_prescale_setup");
        begin_c();
        timer_prescale_setup(after_c);
        check_mem("timer_prescale_setup", 0xF000, 0xF020);

        random_image(0xC0100u + v * 3);
        run_rom(0x0A15A, &args, "key_filter_clear");
        begin_c();
        key_filter_clear(after_c);
        check_mem("key_filter_clear", 0xF040, 0xF048);

        random_image(0xC0200u + v * 3);
        run_rom(0x0A162, &args, "key_output_all");
        begin_c();
        key_output_all(after_c);
        check_mem("key_output_all", 0xF040, 0xF048);

        random_image(0xC0300u + v * 3);
        run_rom(0x0A16A, &args, "key_output_zero");
        begin_c();
        key_output_zero(after_c);
        check_mem("key_output_zero", 0xF040, 0xF048);

        random_image(0xC0400u + v * 3);
        image[0xF040] = values[v];
        expected = run_rom(0x0A172, &args, "key_all_released");
        begin_c();
        uint8_t got = key_all_released(after_c);
        check_ret("key_all_released", expected, got);
        check_mem("key_all_released", 0xF040, 0xF048);
    }
}

static void check_sfr(const char *what, uint32_t impl, uint32_t seed)
{
    random_image(seed);
    Args args = {.er = {0, 0}};
    run_rom(impl, &args, what);
    begin_c();

    if (impl == 0x09A2A)
        keyboard_reset(after_c);
    else if (impl == 0x0A060)
        hw_clear_0F220(after_c);
    else if (impl == 0x0A070)
        hw_clear_0F048(after_c);
    else
        lcd_hw_setup(after_c);
    check_mem(what, 0xF000, 0xF048);
}

static void test_hw_setup(void)
{
    check_sfr("keyboard_reset", 0x09A2A, 0xD0000u + 0);
    check_sfr("hw_clear_0F220", 0x0A060, 0xD0000u + 1);
    check_sfr("hw_clear_0F048", 0x0A070, 0xD0000u + 2);
    check_sfr("lcd_hw_setup", 0x0A0AA, 0xD0000u + 3);

    /* each with several random starting states */
    for (int i = 0; i < 6; i++) {
        check_sfr("keyboard_reset", 0x09A2A, 0xD0100u + i * 4);
        check_sfr("hw_clear_0F220", 0x0A060, 0xD0100u + i * 4 + 1);
        check_sfr("hw_clear_0F048", 0x0A070, 0xD0100u + i * 4 + 2);
        check_sfr("lcd_hw_setup", 0x0A0AA, 0xD0100u + i * 4 + 3);
    }
}

static void test_keycode_cvt(void)
{
    static const uint16_t tables[] = {0x7B0Eu, 0x83DAu};
    static const uint8_t samples[] = {0, 1, 2, 0x55, 0x80, 0xFF};

    size_t t, s, r;
    uint16_t col, row;

    /* every single-bit column against every single-bit row (the sane key
     * matrix cases), on both keycode tables */
    for (t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        for (col = 1; col <= 0x80u; col <<= 1) {
            for (row = 1; row <= 0x80u; row <<= 1) {
                uint16_t rc, cc;

                random_image(0xE0000u + t * 200 + col * row * 4 + row);
                image[0xE320] = (uint8_t)col;
                image[0xE321] = (uint8_t)row;
                Args args = {.er = {0xE320, tables[t]}};
                rc = run_rom(0x29892, &args, "keycode_cvt");
                begin_c();
                cc = keycode_cvt(after_c, 0xE320, tables[t]);
                check_ret("keycode_cvt", rc, cc);
                check_mem("keycode_cvt", 0xE300, 0xE340);
            }
        }
    }

    /* messy inputs: zero bytes, multi-bit masks, all-ones */
    for (t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        for (s = 0; s < sizeof(samples) / sizeof(samples[0]); s++) {
            for (r = 0; r < sizeof(samples) / sizeof(samples[0]); r++) {
                uint16_t rc;

                random_image(0xE1000u + t * 100 + s * 10 + r);
                image[0xE320] = samples[s];
                image[0xE321] = samples[r];
                Args args = {.er = {0xE320, tables[t]}};
                rc = run_rom(0x29892, &args, "keycode_cvt sample");
                begin_c();
                check_ret("keycode_cvt sample", rc,
                          keycode_cvt(after_c, 0xE320, tables[t]));
                check_mem("keycode_cvt sample", 0xE300, 0xE340);
            }
        }
    }

    /* random storage addresses, the result must still line up */
    for (t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        for (col = 0; col < 8; col++) {
            uint16_t rc;

            random_image(0xE2000u + t * 100 + col);
            image[0xE35C] = (uint8_t)(0x10 + col);
            image[0xE35D] = (uint8_t)(0x40 + col * 3);
            Args args = {.er = {0xE35C, tables[t]}};
            rc = run_rom(0x29892, &args, "keycode_cvt odd");
            begin_c();
            check_ret("keycode_cvt odd", rc,
                      keycode_cvt(after_c, 0xE35C, tables[t]));
        }
    }
}

static void test_hw_bringup(void)
{
    static const uint8_t ki[] = {0x00, 0x04, 0xFB, 0xFF};
    uint16_t rc, cc;
    int i;

    /* full boot sequence: both pictures must move the SFR pane the same way */
    for (i = 0; i < 3; i++) {
        random_image(0xE3000u + (unsigned)i);
        Args boot = {.er = {0, 0}};
        run_rom(0x0A012, &boot, "hw_init");
        begin_c();
        hw_init(after_c);
        check_mem("hw_init", 0xF000, 0xF060);
    }

    /* the interrupt gate stubs: data space must not move a bit */
    random_image(0xE3100u);
    Args none = {.er = {0, 0}};
    run_rom(0x0A058, &none, "interrupts_enable");
    begin_c();
    interrupts_enable();
    check_mem("interrupts_enable", 0xF000, 0xF060);

    random_image(0xE3101u);
    run_rom(0x0A05C, &none, "interrupts_disable");
    begin_c();
    interrupts_disable();
    check_mem("interrupts_disable", 0xF000, 0xF060);

    /* ON-key watchdog: bit 2 of 0xF040 decides confirm vs bail-out, and the
     * execution length differs (0 vs 4 waits), so both shapes get covered */
    for (i = 0; i < (int)(sizeof(ki) / sizeof(ki[0])); i++) {
        random_image(0xE3200u + (unsigned)i);
        image[0xF040] = ki[i];
        image[0xF046] = (uint8_t)(0x20 | i);   /* gets overwritten */
        Args none2 = {.er = {0, 0}};
        rc = run_rom(0x0A18A, &none2, "poll_on_key");
        begin_c();
        cc = poll_on_key(after_c);
        check_ret("poll_on_key", (uint16_t)(rc & 0xFF), cc);
        check_mem("poll_on_key", 0xF000, 0xF060);
        check_mem("poll_on_key", 0xD140, 0xD160);
    }
}

void test_keyboard(void)
{
    test_timer_delay();
    test_check_any_key();
    test_key_helpers();
    test_keycode_cvt();
    test_hw_bringup();
    test_hw_setup();
}
