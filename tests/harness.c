/* harness.c - machine setup and comparison helpers. */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t image[IMG_SIZE];
uint8_t after_rom[IMG_SIZE];
uint8_t after_c[IMG_SIZE];
int checks;
int failures;

static Nx8Machine m;
static uint8_t rom[0x40000];
static uint32_t rng_state;

uint32_t rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

void random_image(uint32_t seed)
{
    rng_state = seed * 2654435761u + 1;
    memcpy(image, rom, IMG_SIZE);
    for (uint32_t a = RAM_BASE; a < IMG_SIZE; a++)
        image[a] = (uint8_t)(rnd() & 0xFF);
}

void begin_c(void)
{
    memcpy(after_c, image, IMG_SIZE);
}

static void machine_reset(void)
{
    nx8_init(&m);
    nx8_load_rom(&m, rom, 0x40000);
    memset(m.mem + RAM_BASE, 0, IMG_SIZE - RAM_BASE);
    m.sp = STACK_TOP;
    m.psw = 0;
    m.dsr = 0;
    m.csr = 0;
    m.faulted = 0;
    m.ignored_writes = 0;
    m.steps = 0;
}

uint16_t run_rom(uint32_t addr, const Args *a, const char *what)
{
    machine_reset();
    memcpy(m.mem + RAM_BASE, image + RAM_BASE, IMG_SIZE - RAM_BASE);
    if (a && a->sp)
        m.sp = a->sp;
    for (int i = 0; i < 8; i++)
        nx8_set_er(&m, (unsigned)(i * 2), a ? a->er[i] : 0);
    if (a)
        for (int i = 0; i < a->nstk; i++) {
            m.mem[m.sp + 2 * i] = (uint8_t)(a->stk[i] & 0xFF);
            m.mem[m.sp + 2 * i + 1] = (uint8_t)(a->stk[i] >> 8);
        }
    size_t steps = nx8_call(&m, addr, 8000000);
    if (steps == (size_t)-1) {
        printf("FAIL %-24s ROM call did not return (fault=%d, %s, pc=%04X)\n",
               what, m.faulted, m.fault_msg ? m.fault_msg : "-", m.pc);
        failures++;
        memset(after_rom, 0, sizeof(after_rom));
        return 0;
    }
    memcpy(after_rom, image, IMG_SIZE);
    memcpy(after_rom + RAM_BASE, m.mem + RAM_BASE, IMG_SIZE - RAM_BASE);
    return nx8_er(&m, 0);
}

void check_ret(const char *what, uint16_t rom_ret, uint16_t c_ret)
{
    checks++;
    if (rom_ret != c_ret) {
        failures++;
        printf("FAIL %-24s ret: rom=%04X c=%04X\n", what, rom_ret, c_ret);
    }
}

void check_mem(const char *what, uint32_t from, uint32_t to)
{
    checks++;
    for (uint32_t a = from; a < to; a++) {
        if (after_rom[a] != after_c[a]) {
            failures++;
            printf("FAIL %-24s mem: %04X rom=%02X c=%02X\n",
                   what, a, after_rom[a], after_c[a]);
            return;
        }
    }
}

void harness_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
        fprintf(stderr, "ROM must be %zu bytes\n", sizeof(rom));
        exit(1);
    }
    fclose(f);
}

Nx8Machine *harness_machine(void)
{
    return &m;
}

uint8_t *harness_rom(void)
{
    return rom;
}
