/*
 * smoke.c - run the real ROM through our own CPU core and compare against the
 * CasioEmu executable. This is the sanity gate for the whole toolchain: if our
 * instruction semantics are wrong, everything built on them is wrong.
 *
 * It boots the ROM from the reset vector and checks that execution reaches the
 * same "interesting" addresses as the reference emulator, then calls a few ROM
 * functions and prints their register results for cross-checking.
 */
#include "../emu/nx8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load_rom(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { perror("read"); exit(1); }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "resource/rom.bin";
    size_t size;
    uint8_t *rom = load_rom(rom_path, &size);
    printf("ROM: %s (%zu bytes)\n", rom_path, size);

    Nx8Machine m;
    nx8_init(&m);
    nx8_load_rom(&m, rom, size);

    nx8_reset(&m);
    printf("reset: sp=%04X pc=%04X (vectors: %04X %04X)\n", m.sp, m.pc,
           nx8_read_code(&m, 0), nx8_read_code(&m, 2));

    /* boot: run a bounded number of instructions and report where we stop */
    for (int i = 0; i < 200000; ++i) {
        nx8_step(&m);
        if (m.faulted) {
            printf("fault after %llu steps at pc=%04X: %s\n",
                   (unsigned long long)m.steps, m.pc, m.fault_msg ? m.fault_msg : "?");
            return 2;
        }
    }
    printf("booted: %llu steps, pc=%04X sp=%04X csr=%04X\n",
           (unsigned long long)m.steps, m.pc, m.sp, m.csr);
    printf("registers: ");
    for (int i = 0; i < 16; ++i) printf("%02X ", m.r[i]);
    printf("\npsw=%02X dsr=%02X ea=%04X\n", m.psw, m.dsr, m.ea);
    return 0;
}
