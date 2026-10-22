/* harness.h - shared differential-test harness.
 *
 * Each test puts the machine into a fully defined state, runs a ROM function
 * in the C nX-U8/100 core, then runs the C reimplementation on a byte image
 * with identical contents and compares the results.
 */
#ifndef NX_TEST_HARNESS_H
#define NX_TEST_HARNESS_H

#include <stdint.h>

#include "../emu/nx8.h"

#define IMG_SIZE 0x10000
#define RAM_BASE 0xD000
#define CMP_END 0xEC00
#define STACK_TOP 0xED00

extern uint8_t image[IMG_SIZE];
extern uint8_t after_rom[IMG_SIZE];
extern uint8_t after_c[IMG_SIZE];
extern int checks;
extern int failures;

typedef struct {
    uint16_t er[8];      /* er0, er2, ... er14 */
    uint16_t stk[4];     /* stack arguments, [sp], [sp+2], ... */
    int nstk;
    uint16_t sp;         /* 0 = default (STACK_TOP), else custom */
} Args;

uint32_t rnd(void);
void random_image(uint32_t seed);
void begin_c(void);
void harness_load_rom(const char *path);
Nx8Machine *harness_machine(void);
uint8_t *harness_rom(void);

uint16_t run_rom(uint32_t addr, const Args *a, const char *what);
void check_ret(const char *what, uint16_t rom_ret, uint16_t c_ret);
void check_mem(const char *what, uint32_t from, uint32_t to);

void test_string(void);
void test_mem(void);
void test_display(void);
void test_font(void);
void test_print(void);
void test_keyboard(void);
void test_emu(void);

#endif
