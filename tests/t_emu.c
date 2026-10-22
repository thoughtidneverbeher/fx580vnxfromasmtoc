/* t_emu.c - instruction-level semantics of the C nX-U8/100 core.
 *
 * These tests do not touch ROM functions: they execute hand-encoded
 * instructions and check the architectural state.  They exist mainly to pin
 * down addressing modes whose behaviour is easy to get wrong, in particular
 * the difference between `[ea]` and `[ea+]` (only the latter post-increments)
 * and the non-incrementing direct / register / frame-relative forms.
 */
#include "harness.h"

#include <stdio.h>
#include <string.h>

#define CODE_ADDR 0x30000u

static Nx8Machine *m;

static void expect(const char *what, unsigned got, unsigned want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %-32s got=%04X want=%04X\n", what, got, want);
    }
}

static void reset(uint16_t ea, uint16_t r0, uint16_t r2, uint16_t r14)
{
    m = harness_machine();
    nx8_init(m);
    nx8_load_rom(m, harness_rom(), 0x40000);
    memset(m->mem, 0, sizeof(m->mem));
    /* A recognisable pattern in RAM. */
    for (unsigned i = 0; i < 0x100; i++)
        m->mem[0xD100 + i] = (uint8_t)(i * 7u + 1u);
    m->mem[0xD1FE] = 0x5A;
    m->mem[0xD1FF] = 0xA5;
    m->sp = STACK_TOP;
    m->psw = 0;
    m->dsr = 0;
    m->ea = ea;
    nx8_set_er(m, 0, r0);
    nx8_set_er(m, 2, r2);
    nx8_set_er(m, 14, r14);
}

/* Run `code` (terminated by `rt`) placed at CODE_ADDR in the code segment. */
static void run(const uint8_t *code, size_t n)
{
    memcpy(m->rom + CODE_ADDR, code, n);
    size_t steps = nx8_call(m, CODE_ADDR, 100);
    if (steps == (size_t)-1) {
        checks++;
        failures++;
        printf("FAIL instruction sequence did not return (pc=%04X)\n", m->pc);
    }
}

static void t_load_ea(void)
{
    /* l r0, [ea] : reads mem[ea], leaves ea alone. */
    static const uint8_t c[] = { 0x30, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(c, sizeof c);
    expect("l r0,[ea] value", m->r[0], 0x01);
    expect("l r0,[ea] ea", m->ea, 0xD100);

    /* l r0, [ea+] : reads mem[ea], then ea += 1. */
    static const uint8_t cp[] = { 0x50, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(cp, sizeof cp);
    expect("l r0,[ea+] value", m->r[0], 0x01);
    expect("l r0,[ea+] ea", m->ea, 0xD101);

    /* l er0, [ea] : aligned 16-bit read, no bump. */
    static const uint8_t ce[] = { 0x32, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(ce, sizeof ce);
    expect("l er0,[ea] value", nx8_er(m, 0), 0x0801);
    expect("l er0,[ea] ea", m->ea, 0xD100);

    /* l er0, [ea+] : odd ea is aligned down; ea += 2 then aligned down. */
    static const uint8_t cep[] = { 0x52, 0x90, 0x1F, 0xFE };
    reset(0xD101, 0x1234, 0, 0);
    run(cep, sizeof cep);
    expect("l er0,[ea+] value", nx8_er(m, 0), 0x0801);
    expect("l er0,[ea+] ea", m->ea, 0xD102);

    /* l r0, [d_xxxx] : direct address, no ea change. */
    static const uint8_t cd[] = { 0x10, 0x90, 0xFE, 0xD1, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(cd, sizeof cd);
    expect("l r0,[d] value", m->r[0], 0x5A);
    expect("l r0,[d] ea", m->ea, 0xD100);

    /* l r0, 2[er14] : frame-relative, no ea change. */
    static const uint8_t cf[] = { 0x42, 0xD0, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0xD100);
    run(cf, sizeof cf);
    expect("l r0,2[er14] value", m->r[0], 0x0F);
    expect("l r0,2[er14] ea", m->ea, 0xD100);

    /* l r0, 2[er12] : base-pointer relative (er12), no ea change. */
    static const uint8_t cb[] = { 0x02, 0xD0, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    nx8_set_er(m, 12, 0xD100);
    run(cb, sizeof cb);
    expect("l r0,2[er12] value", m->r[0], 0x0F);
    expect("l r0,2[er12] ea", m->ea, 0xD100);

    /* l r0, [er2] : register indirect, no ea change. */
    static const uint8_t cr[] = { 0x20, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0xD100, 0);
    run(cr, sizeof cr);
    expect("l r0,[er2] value", m->r[0], 0x01);
    expect("l r0,[er2] ea", m->ea, 0xD100);
}

static void t_store_ea(void)
{
    /* st r0, [ea] : writes mem[ea], leaves ea alone. */
    static const uint8_t c[] = { 0x31, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(c, sizeof c);
    expect("st r0,[ea] value", m->mem[0xD100], 0x34);
    expect("st r0,[ea] ea", m->ea, 0xD100);

    /* st r0, [ea+] : writes then ea += 1. */
    static const uint8_t cp[] = { 0x51, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(cp, sizeof cp);
    expect("st r0,[ea+] value", m->mem[0xD100], 0x34);
    expect("st r0,[ea+] ea", m->ea, 0xD101);

    /* st er0, [ea] : 16-bit store, no bump. */
    static const uint8_t ce[] = { 0x33, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(ce, sizeof ce);
    expect("st er0,[ea] lo", m->mem[0xD100], 0x34);
    expect("st er0,[ea] hi", m->mem[0xD101], 0x12);
    expect("st er0,[ea] ea", m->ea, 0xD100);

    /* st er0, [ea+] : writes, then ea += 2. */
    static const uint8_t cep[] = { 0x53, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(cep, sizeof cep);
    expect("st er0,[ea+] lo", m->mem[0xD100], 0x34);
    expect("st er0,[ea+] hi", m->mem[0xD101], 0x12);
    expect("st er0,[ea+] ea", m->ea, 0xD102);

    /* st r0, [d_xxxx] : direct address, no ea change. */
    static const uint8_t cd[] = { 0x11, 0x90, 0xFE, 0xD1, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    run(cd, sizeof cd);
    expect("st r0,[d] value", m->mem[0xD1FE], 0x34);
    expect("st r0,[d] ea", m->ea, 0xD100);

    /* st r0, 2[er14] : frame-relative, no ea change. */
    static const uint8_t cf[] = { 0xC2, 0xD0, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0xD100);
    run(cf, sizeof cf);
    expect("st r0,2[er14] value", m->mem[0xD102], 0x34);
    expect("st r0,2[er14] ea", m->ea, 0xD100);

    /* st r0, 2[er12] : base-pointer relative, no ea change. */
    static const uint8_t cb[] = { 0x82, 0xD0, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0, 0);
    nx8_set_er(m, 12, 0xD100);
    run(cb, sizeof cb);
    expect("st r0,2[er12] value", m->mem[0xD102], 0x34);
    expect("st r0,2[er12] ea", m->ea, 0xD100);

    /* st r0, [er2] : register indirect, no ea change. */
    static const uint8_t cr[] = { 0x21, 0x90, 0x1F, 0xFE };
    reset(0xD100, 0x1234, 0xD100, 0);
    run(cr, sizeof cr);
    expect("st r0,[er2] value", m->mem[0xD100], 0x34);
    expect("st r0,[er2] ea", m->ea, 0xD100);
}

static void t_inc_dec_ea(void)
{
    /* inc [ea] / dec [ea] modify the byte but never move or align ea. */
    static const uint8_t c[] = { 0x2F, 0xFE, 0x1F, 0xFE };
    reset(0xD101, 0, 0, 0);
    run(c, sizeof c);
    expect("inc [ea] value", m->mem[0xD101], 0x09);
    expect("inc [ea] ea", m->ea, 0xD101);

    static const uint8_t d[] = { 0x3F, 0xFE, 0x1F, 0xFE };
    reset(0xD101, 0, 0, 0);
    run(d, sizeof d);
    expect("dec [ea] value", m->mem[0xD101], 0x07);
    expect("dec [ea] ea", m->ea, 0xD101);
}

void test_emu(void)
{
    t_load_ea();
    t_store_ea();
    t_inc_dec_ea();
}
