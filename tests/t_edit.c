/* t_edit.c - differential tests for the edit-box support routines. */
#include "harness.h"

#include "../src/edit.h"

static void test_prev_unit_off(void)
{
    /* the single byte sitting at probe-2 decides between the one-byte unit
     * result (probe-1) and the marker-byte result (probe-2) */
    static const uint8_t tails[] = {0x00, 0x24, 0x7F, 0xEF, 0xF0, 0xFF, 0xA6, 0x80};
    static const uint16_t probes[] = {0xD004u, 0xD005u, 0xD008u, 0xD00Bu, 0xD010u, 0xD011u};
    static const uint16_t los[] = {0xD000u, 0xD001u, 0xD002u, 0x0000u, 0xFFF1u, 0x8010u, 0xD011u};

    for (size_t p = 0; p < sizeof(probes) / sizeof(probes[0]); p++) {
        for (size_t li = 0; li < sizeof(los) / sizeof(los[0]); li++) {
            for (size_t t = 0; t < sizeof(tails); t++) {
                uint16_t probe = probes[p];
                uint16_t lo = los[li];
                uint16_t rc, cc;

                random_image(0x40000u + (unsigned)(p * 80 + li * 9 + t));
                image[probe - 2] = tails[t];
                image[probe - 1] = (uint8_t)~tails[t];   /* never read */
                Args args = {.er = {lo, probe}};
                rc = run_rom(0x0AC60, &args, "prev_unit_off");
                begin_c();
                cc = prev_unit_off(after_c, lo, probe);
                check_ret("prev_unit_off", rc, cc);
                check_mem("prev_unit_off", 0xD000, 0xD020);
            }
        }
    }
}

void test_edit(void)
{
    test_prev_unit_off();
}
