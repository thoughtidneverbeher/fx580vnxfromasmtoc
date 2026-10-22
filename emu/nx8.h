/*
 * nx8.h - minimal, faithful nX-U8/100 CPU core for the Casio fx-580VN X.
 *
 * This exists for one purpose: differential testing of the C reimplementation
 * against the real ROM. Tests call a ROM function here, call our C function on
 * the same machine state, and compare the resulting state byte for byte.
 *
 * Semantics are ported from CasioEmu (machine/emulator/src/Chipset/CPU*.cpp) and
 * the memory map from the same project's peripherals. The instruction table is
 * generated from tools/nx8.py (tools/gen_opcode_table.py), which is validated
 * against the Lua disassembler over the entire ROM.
 */
#ifndef NX8_H
#define NX8_H

#include <stdint.h>
#include <stddef.h>

#define NX8_MEM_SIZE 0x100000u   /* 24-bit address space */

/* PSW flags (CPU.hpp) */
#define PSW_C 0x80
#define PSW_Z 0x40
#define PSW_S 0x20
#define PSW_OV 0x10
#define PSW_MIE 0x08
#define PSW_HC 0x04
#define PSW_ELEVEL 0x03

/* instruction hint flags (CPU.hpp) */
#define H_IE 0x0001
#define H_ST 0x0002
#define H_DW 0x0004
#define H_DS 0x0008
#define H_IA 0x0010
#define H_TI 0x0020
#define H_WB 0x0040

typedef struct Nx8Machine {
    uint8_t rom[0x40000];        /* code space image (the flash) */
    size_t rom_size;
    uint8_t mem[0x10000];        /* data space: RAM 0xD000, SFR, screen 0xF800 */
    uint8_t writable[0x10000 / 8];

    /* register file: 16 8-bit registers, paired into er/xr/qr views */
    uint8_t r[16];
    uint16_t cr[16];             /* coprocessor registers (16-bit) */
    uint16_t pc;
    uint16_t csr;                /* code segment register */
    uint16_t sp;
    uint16_t ea;
    uint8_t psw;
    uint8_t dsr;
    uint16_t lr, lcsr;
    uint16_t elr[4], ecsr[4];
    uint8_t epsw[4];

    /* execution context of the current instruction */
    uint16_t opcode, imm16;
    uint64_t operands[2];
    size_t operand_regindex[2], operand_regsize[2];
    size_t hint;
    uint8_t last_dsr;
    uint8_t shift_buffer;
    uint8_t flags_changed, flags_in, flags_out;

    uint64_t steps;              /* instructions executed */
    uint64_t step_limit;         /* 0 = unlimited */
    uint64_t ignored_writes;     /* writes dropped by the memory map */
    int faulted;                 /* non-zero after an unmapped/odd access */
    const char *fault_msg;
} Nx8Machine;

/* construction / memory map */
void nx8_init(Nx8Machine *m);
void nx8_load_rom(Nx8Machine *m, const uint8_t *rom, size_t size);
void nx8_set_writable(Nx8Machine *m, uint32_t base, uint32_t size);
uint8_t nx8_read(const Nx8Machine *m, uint32_t addr);
void nx8_write(Nx8Machine *m, uint32_t addr, uint8_t value);
uint16_t nx8_read_code(Nx8Machine *m, uint32_t addr);

/* register views: rN (8-bit), erN (16-bit). Register pairs are adjacent bytes,
 * exactly like CPU.hpp's register file (erN == rN low byte, rN+1 high byte). */
static inline uint16_t nx8_er(const Nx8Machine *m, unsigned n) {
    return (uint16_t)(m->r[n] | (m->r[(n + 1) & 15] << 8));
}
static inline void nx8_set_er(Nx8Machine *m, unsigned n, uint16_t v) {
    m->r[n] = (uint8_t)v;
    m->r[(n + 1) & 15] = (uint8_t)(v >> 8);
}

/* execution */
void nx8_reset(Nx8Machine *m);       /* vectors: sp = word[0], pc = word[1] */
void nx8_step(Nx8Machine *m);        /* one instruction (dsr prefixes included) */

/*
 * Call a ROM function and return when it comes back.
 * lr is set to NX8_SENTINEL (a `bc al` self-loop in ROM), lcsr to 0, pc to
 * `addr`. Works for both `rt` and `pop pc` epilogues. Returns the number of
 * instructions executed, or (size_t)-1 if the function never returned.
 */
#define NX8_SENTINEL 0x0000A1D0u
size_t nx8_call(Nx8Machine *m, uint32_t addr, uint64_t max_steps);

/* diagnostics */
const char *nx8_disasm(const Nx8Machine *m, uint32_t addr, char *buf, size_t buflen);

#endif /* NX8_H */
