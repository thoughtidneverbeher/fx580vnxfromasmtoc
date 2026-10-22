/* t_display.c - differential tests for the screen-buffer cluster. */
#include "harness.h"

#include "../src/display.h"

static void test_buffer_clear(void)
{
    for (int t = 0; t < 3; t++) {
        random_image((uint32_t)(1000 + t));
        Args a = {.nstk = 0};
        run_rom(0x08C60, &a, "buffer_clear");
        begin_c();
        buffer_clear(after_c);
        check_mem("buffer_clear", RAM_BASE, CMP_END);
        check_mem("buffer_clear", 0xF800, 0x10000);
    }
}

static void test_buf_copy(void)
{
    for (int k = 0; k < 2; k++) {
        random_image((uint32_t)(1010 + k));
        Args a = {.nstk = 0};
        if (k == 0) {
            run_rom(0x09440, &a, "buf1_to_buf2");
            begin_c();
            buf1_to_buf2(after_c);
            check_mem("buf1_to_buf2", RAM_BASE, CMP_END);
        } else {
            run_rom(0x09458, &a, "buf2_to_buf1");
            begin_c();
            buf2_to_buf1(after_c);
            check_mem("buf2_to_buf1", RAM_BASE, CMP_END);
        }
    }
}

static void test_fill_screen(void)
{
    static const uint16_t args[] = {
        0x0000, 0x0001, 0x0100, 0x0101, 0x0200, 0x0201, 0x0400, 0x0500,
        0x0600, 0xFFFF, 0x00AA, 0x01AA, 0x02AA, 0x04AA, 0x07AA,
    };
    for (size_t t = 0; t < sizeof(args) / sizeof(args[0]); t++) {
        random_image((uint32_t)(1020 + t));
        Args a = {.er = {args[t]}, .nstk = 0};
        run_rom(0x08C0C, &a, "fill_screen");
        begin_c();
        fill_screen(after_c, args[t]);
        check_mem("fill_screen", RAM_BASE, CMP_END);
        check_mem("fill_screen", 0xF800, 0x10000);
    }
}

static void test_render(void)
{
    for (int t = 0; t < 8; t++) {
        random_image((uint32_t)(1040 + t));
        /* exercise every combination of the two lcd_update flags */
        image[0xD0F5] = (t & 1) ? 0 : 1;
        image[0xD0F7] = (t & 2) ? 1 : 0;
        Args a = {.nstk = 0};
        if (t < 4) {
            run_rom(0x09470, &a, "render_e3d4");
            begin_c();
            render_e3d4(after_c);
            check_mem("render_e3d4", RAM_BASE, CMP_END);
            check_mem("render_e3d4", 0xF800, 0x10000);
        } else {
            image[0xD0F5] = (t & 1) ? 0 : 1;
            image[0xD0F7] = (t & 2) ? 1 : 0;
            run_rom(0x0947C, &a, "render_ddd4");
            begin_c();
            render_ddd4(after_c);
            check_mem("render_ddd4", RAM_BASE, CMP_END);
            check_mem("render_ddd4", 0xF800, 0x10000);
        }
    }
}

void test_display(void)
{
    test_buffer_clear();
    test_buf_copy();
    test_fill_screen();
    test_render();
}
