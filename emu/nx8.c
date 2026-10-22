/*
 * nx8.c - see nx8.h. Ported from CasioEmu's Chipset/CPU*.cpp (same semantics),
 * with the instruction table generated from tools/nx8.py.
 */
#include "nx8.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* operand / handler codes (must match tools/gen_opcode_table.py)      */
/* ------------------------------------------------------------------ */
enum {
    F_NONE, F_IM, F_BO, F_JO, F_R, F_ER, F_XR, F_QR, F_CR, F_CER, F_CXR,
    F_CQR, F_DSR, F_EA, F_EAP, F_ELR, F_LR, F_PSW, F_EPSW, F_ECSR, F_SP,
    F_PC, F_CO, F_LAB, F_DLAB, F_PUSH, F_POP
};

enum {
    H_ADD, H_ADD16, H_ADDC, H_AND, H_CMP, H_CMPC, H_MOV, H_MOV16, H_OR, H_XOR,
    H_SUB, H_SUBC, H_SLL, H_SLLC, H_SRA, H_SRL, H_SRLC,
    H_LS_EA, H_LS_R, H_LS_I_R, H_LS_BP, H_LS_FP, H_LS_I,
    H_ADDSP, H_CTRL, H_PUSH, H_PUSHL, H_POP, H_POPL,
    H_CR_R, H_CR_EA, H_LEA, H_DAA, H_DAS, H_NEG, H_BITMOD,
    H_PSW_OR, H_PSW_AND, H_CPLC, H_BC, H_EXTBW, H_SWI, H_BRK,
    H_B, H_BL, H_MUL, H_DIV, H_INC_EA, H_DEC_EA, H_RT, H_RTI,
    H_NOP, H_DSR
};

typedef struct {
    uint16_t opmask;
    uint16_t varying;
    uint16_t hint;
    uint8_t handler;
    uint8_t has_imm;
    uint8_t offsetable;
    struct {
        uint8_t fmt;
        uint8_t shift;
        uint16_t mask;
        uint8_t regsize;
    } ops[3];
} OpDesc;

#include "opcode_table.inc"

static const OpDesc *dispatch[0x10000];
static int dispatch_ready = 0;

/* ------------------------------------------------------------------ */
/* memory map                                                          */
/* ------------------------------------------------------------------ */
void nx8_set_writable(Nx8Machine *m, uint32_t base, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t a = base + i;
        if (a < 0x10000)
            m->writable[a >> 3] |= (uint8_t)(1u << (a & 7));
    }
}

static int is_writable(const Nx8Machine *m, uint32_t a)
{
    return (m->writable[a >> 3] >> (a & 7)) & 1;
}

/*
 * Data space (dsr = 0 for all firmware data):
 *   0x0000-0xCFFF  ROM (read only)
 *   0xD000-0xEFFF  battery-backed RAM
 *   0xF000-0xF0FF  SFRs
 *   0xF800-0xFFFF  screen buffer
 * Segment 5 (0x50000) is the ROM window; segments 1-3 are ROM too.
 */
static uint8_t data_read(const Nx8Machine *m, uint32_t addr)
{
    uint32_t seg = addr >> 16;
    uint32_t off = addr & 0xFFFF;
    if (seg == 0) {
        if (off < 0xD000)
            return m->rom[off];
        return m->mem[off];
    }
    if (seg <= 3)
        return m->rom[addr & 0x3FFFF];
    if (seg == 5)
        return m->rom[off];
    return 0;
}

uint8_t nx8_read(const Nx8Machine *m, uint32_t addr)
{
    return data_read(m, addr);
}

void nx8_write(Nx8Machine *m, uint32_t addr, uint8_t value)
{
    uint32_t seg = addr >> 16;
    uint32_t off = addr & 0xFFFF;
    if (seg == 0 && off >= 0xD000 && is_writable(m, off)) {
        m->mem[off] = value;
        return;
    }
    /* The real chip silently drops writes to ROM and open bus; the
     * firmware relies on that for table-driven stores. */
    m->ignored_writes++;
}

uint16_t nx8_read_code(Nx8Machine *m, uint32_t addr)
{
    uint32_t seg = addr >> 16;
    uint32_t idx;
    if (seg == 0)
        idx = addr & 0xFFFF;
    else if (seg <= 3)
        idx = addr & 0x3FFFF;
    else if (seg == 5)
        idx = addr & 0xFFFF;
    else {
        m->faulted = 3;
        m->fault_msg = "code fetch from unmapped segment";
        return 0xFFFF;
    }
    if (idx + 1 >= m->rom_size) {
        m->faulted = 4;
        m->fault_msg = "code fetch beyond ROM";
        return 0xFFFF;
    }
    return (uint16_t)(m->rom[idx] | (m->rom[idx + 1] << 8));
}

void nx8_init(Nx8Machine *m)
{
    memset(m, 0, sizeof(*m));
    m->step_limit = 0;
    nx8_set_writable(m, 0x0D000, 0x2100);   /* RAM + SFRs */
    nx8_set_writable(m, 0x0F800, 0x0800);   /* screen buffer */
    if (!dispatch_ready) {
        /* CasioEmu expands a 64K permutation buffer per entry; some operand
         * fields (e.g. `add rN, imm8`) legitimately cover 4096 opcodes. */
        static uint16_t keys[0x10000];
        for (size_t i = 0; i < OPCODE_SOURCE_COUNT; ++i) {
            const OpDesc *d = &opcode_sources[i];
            size_t count = 1;
            keys[0] = d->opmask;
            for (uint16_t bit = 0x8000; bit; bit >>= 1) {
                if (d->varying & bit) {
                    for (size_t k = 0; k < count; ++k)
                        keys[k + count] = (uint16_t)(keys[k] | bit);
                    count <<= 1;
                }
            }
            for (size_t k = 0; k < count; ++k)
                dispatch[keys[k]] = d;
        }
        dispatch_ready = 1;
    }
}

void nx8_load_rom(Nx8Machine *m, const uint8_t *rom, size_t size)
{
    if (size > sizeof(m->rom))
        size = sizeof(m->rom);
    memcpy(m->rom, rom, size);
    m->rom_size = size;
}

void nx8_reset(Nx8Machine *m)
{
    m->sp = nx8_read_code(m, 0);
    m->dsr = 0;
    m->psw = 0;
    m->csr = 0;
    m->pc = nx8_read_code(m, 2);
}

/* ------------------------------------------------------------------ */
/* flag helpers (CPUArithmetic.cpp)                                    */
/* ------------------------------------------------------------------ */
static void zs_check(Nx8Machine *m)
{
    m->flags_changed |= PSW_Z | PSW_S;
    if (m->operands[0] & 0xFF)
        m->flags_out &= (uint8_t)~PSW_Z;
    m->flags_out = (uint8_t)((m->flags_out & ~PSW_S) | ((m->operands[0] & 0x80) ? PSW_S : 0));
}

static void add8(Nx8Machine *m)
{
    uint8_t op0 = (uint8_t)m->operands[0], op1 = (uint8_t)m->operands[1];
    uint16_t c_in = (m->flags_in & PSW_C) ? 1 : 0;
    int carry8 = (((uint16_t)op0 & 0xFF) + (op1 & 0xFF) + c_in) >> 8;
    int carry7 = (((uint16_t)op0 & 0x7F) + (op1 & 0x7F) + c_in) >> 7;
    int carry4 = (((uint16_t)op0 & 0x0F) + (op1 & 0x0F) + c_in) >> 4;

    m->flags_changed |= PSW_C | PSW_OV | PSW_HC;
    m->flags_out = (uint8_t)((m->flags_out & ~PSW_C) | (carry8 ? PSW_C : 0));
    m->flags_out = (uint8_t)((m->flags_out & ~PSW_OV) | ((carry8 ^ carry7) ? PSW_OV : 0));
    m->flags_out = (uint8_t)((m->flags_out & ~PSW_HC) | (carry4 ? PSW_HC : 0));
    m->operands[0] = (uint8_t)(op0 + op1 + c_in);
}

static void shift_left8(Nx8Machine *m)
{
    m->operands[0] &= 0xFF;
    size_t n = m->operands[1] & 7;
    uint16_t r = (uint16_t)((uint16_t)m->operands[0] << n);
    r |= (uint16_t)(m->shift_buffer >> (8 - n));
    m->flags_changed |= PSW_C;
    if (r & 0x100)
        m->flags_out |= PSW_C;
    m->operands[0] = (uint8_t)r;
}

static void shift_right8(Nx8Machine *m)
{
    m->operands[0] &= 0xFF;
    size_t n = m->operands[1] & 7;
    uint16_t r = (uint16_t)((uint16_t)m->operands[0] << (8 - n));
    r |= (uint16_t)((uint16_t)m->shift_buffer << (16 - n));
    m->flags_changed |= PSW_C;
    if (r & 0x80)
        m->flags_out |= PSW_C;
    m->operands[0] = (uint8_t)(r >> 8);
}

/* ------------------------------------------------------------------ */
/* instruction handlers                                                */
/* ------------------------------------------------------------------ */
static void op_add(Nx8Machine *m);
static void op_addc(Nx8Machine *m);
static void op_sub(Nx8Machine *m);
static void op_subc(Nx8Machine *m);
static void zs_check(Nx8Machine *m);
static void add8(Nx8Machine *m);
static void op_mov(Nx8Machine *m);

static void op_add(Nx8Machine *m)
{
    m->flags_in &= (uint8_t)~PSW_C;
    m->flags_in |= PSW_Z;
    op_addc(m);
}

static void op_addc(Nx8Machine *m)
{
    add8(m);
    if (!(m->flags_in & PSW_Z))
        m->flags_out &= (uint8_t)~PSW_Z;
    zs_check(m);
}

static void op_sub(Nx8Machine *m)
{
    m->flags_in &= (uint8_t)~PSW_C;
    m->flags_in |= PSW_Z;
    op_subc(m);
}

static void op_subc(Nx8Machine *m)
{
    m->operands[0] ^= 0xFF;
    add8(m);
    m->operands[0] ^= 0xFF;
    if (!(m->flags_in & PSW_Z))
        m->flags_out &= (uint8_t)~PSW_Z;
    zs_check(m);
}

static void op_add16(Nx8Machine *m)
{
    if (m->hint & H_IE)
        m->operands[1] |= (m->operands[1] & 0x40) ? 0xFF80 : 0;
    m->flags_in &= (uint8_t)~PSW_C;
    uint8_t hi0 = (uint8_t)(m->operands[0] >> 8), hi1 = (uint8_t)(m->operands[1] >> 8);
    add8(m);
    zs_check(m);
    m->flags_in = (uint8_t)((m->flags_in & ~PSW_C) | (m->flags_out & PSW_C));
    uint8_t lo0 = (uint8_t)m->operands[0];
    m->operands[0] = hi0;
    m->operands[1] = hi1;
    add8(m);
    zs_check(m);
    m->operands[0] = (m->operands[0] << 8) | lo0;
}

static void op_mov16(Nx8Machine *m)
{
    if (m->hint & H_IE)
        m->operands[1] |= (m->operands[1] & 0x40) ? 0xFF80 : 0;
    m->operands[0] = m->operands[1] & 0xFF;
    zs_check(m);
    uint8_t lo0 = (uint8_t)m->operands[0];
    m->operands[0] = (m->operands[1] >> 8) & 0xFF;
    zs_check(m);
    m->operands[0] = (m->operands[0] << 8) | lo0;
}

static void op_mov(Nx8Machine *m) { m->operands[0] = m->operands[1] & 0xFF; zs_check(m); }

static void op_and(Nx8Machine *m) { m->operands[0] &= m->operands[1] & 0xFF; zs_check(m); }
static void op_or(Nx8Machine *m)  { m->operands[0] |= m->operands[1] & 0xFF; zs_check(m); }
static void op_xor(Nx8Machine *m) { m->operands[0] ^= m->operands[1] & 0xFF; zs_check(m); }

static void op_cmp16(Nx8Machine *m)
{
    m->flags_in &= (uint8_t)~PSW_C;
    uint8_t hi0 = (uint8_t)(m->operands[0] >> 8), hi1 = (uint8_t)(m->operands[1] >> 8);
    m->operands[0] ^= 0xFF;
    add8(m);
    m->operands[0] ^= 0xFF;
    zs_check(m);
    m->flags_in = (uint8_t)((m->flags_in & ~PSW_C) | (m->flags_out & PSW_C));
    uint8_t lo0 = (uint8_t)m->operands[0];
    m->operands[0] = hi0;
    m->operands[1] = hi1;
    m->operands[0] ^= 0xFF;
    add8(m);
    m->operands[0] ^= 0xFF;
    zs_check(m);
    m->operands[0] = (m->operands[0] << 8) | lo0;
}

static void load_store(Nx8Machine *m, uint16_t offset, size_t length)
{
    if (length % 2 == 0)
        offset &= (uint16_t)~1;
    size_t base = m->operand_regindex[0];
    if (m->hint & H_ST) {
        for (size_t ix = length - 1; ix != (size_t)-1; --ix)
            nx8_write(m, ((uint32_t)m->dsr << 16) | (uint16_t)(offset + ix),
                      m->r[base + ix]);
    } else {
        for (size_t ix = 0; ix < length; ++ix) {
            m->operands[0] = nx8_read(m, ((uint32_t)m->dsr << 16) | (uint16_t)(offset + ix));
            zs_check(m);
            m->r[base + ix] = (uint8_t)m->operands[0];
        }
    }
    if (m->hint & H_IA) {
        m->ea += (uint16_t)length;
        if (length != 1)
            m->ea &= (uint16_t)~1;
    }
}

static void bump_ea(Nx8Machine *m, size_t value_size)
{
    m->ea += (uint16_t)value_size;
    if (value_size != 1)
        m->ea &= (uint16_t)~1;
}

static void push16(Nx8Machine *m, uint16_t data)
{
    m->sp -= 2;
    nx8_write(m, m->sp + 1, (uint8_t)(data >> 8));
    nx8_write(m, m->sp, (uint8_t)(data & 0xFF));
}

static uint16_t pop16(Nx8Machine *m)
{
    uint16_t v = (uint16_t)(nx8_read(m, m->sp) | (nx8_read(m, m->sp + 1) << 8));
    m->sp += 2;
    return v;
}

static void op_addsp(Nx8Machine *m)
{
    m->operands[0] |= (m->operands[0] & 0x80) ? 0xFF00 : 0;
    m->sp = (uint16_t)(m->sp + m->operands[0]);
}

static void op_ctrl(Nx8Machine *m)
{
    switch (m->hint >> 8) {
    case 1: m->ecsr[m->psw & PSW_ELEVEL] = (uint16_t)m->operands[1]; break;
    case 2: m->elr[m->psw & PSW_ELEVEL] = (uint16_t)m->operands[1]; break;
    case 3: if (m->psw & PSW_ELEVEL) m->epsw[m->psw & PSW_ELEVEL] = (uint8_t)m->operands[1]; break;
    case 4: m->operands[0] = m->elr[m->psw & PSW_ELEVEL]; break;
    case 5: m->operands[0] = m->sp; break;
    case 6: case 7: m->psw = (uint8_t)m->operands[1]; break;
    case 8: m->operands[0] = m->ecsr[m->psw & PSW_ELEVEL]; break;
    case 9: if (m->psw & PSW_ELEVEL) m->operands[0] = m->epsw[m->psw & PSW_ELEVEL]; break;
    case 10: m->operands[0] = m->psw; break;
    case 11: m->sp = (uint16_t)m->operands[1]; break;
    }
}

static void op_lea(Nx8Machine *m)
{
    m->ea = 0;
    if (m->operand_regsize[1])
        m->ea = (uint16_t)m->operands[1];
    if (m->hint & H_TI)
        m->ea = (uint16_t)(m->ea + m->imm16);
}

static void op_daa(Nx8Machine *m)
{
    m->operands[1] = 0;
    if ((m->operands[0] & 0x0F) > 0x09 || (m->flags_in & PSW_HC)) m->operands[1] |= 0x06;
    if ((m->operands[0] & 0xF0) > 0x90 || (m->flags_in & PSW_C)) m->operands[1] |= 0x60;
    if ((m->operands[0] & 0xF0) == 0x90 && (m->operands[0] & 0x0F) > 0x09 && !(m->flags_in & PSW_HC))
        m->operands[1] |= 0x60;
    uint8_t backup = m->flags_in;
    op_add(m);
    m->flags_out |= backup & PSW_C;
    m->flags_changed &= (uint8_t)~PSW_OV;
}

static void op_das(Nx8Machine *m)
{
    m->operands[1] = 0;
    if ((m->operands[0] & 0x0F) > 0x09 || (m->flags_in & PSW_HC)) m->operands[1] |= 0x06;
    if ((m->operands[0] & 0xF0) > 0x90 || (m->flags_in & PSW_C)) m->operands[1] |= 0x60;
    uint8_t backup = m->flags_in;
    op_sub(m);
    m->flags_out |= backup & PSW_C;
    m->flags_changed &= (uint8_t)~PSW_OV;
}

static void op_neg(Nx8Machine *m)
{
    m->operands[1] = m->operands[0];
    m->operands[0] = 0;
    op_sub(m);
}

static void op_bitmod(Nx8Machine *m)
{
    size_t src_index;
    uint64_t bit = 1u << m->operands[1];
    if (m->hint & H_TI) {
        src_index = m->imm16;
        m->operands[0] = nx8_read(m, ((uint32_t)m->dsr << 16) | src_index);
    } else {
        src_index = m->operand_regindex[0];
        m->operands[0] = m->r[src_index];
    }
    m->flags_changed |= PSW_Z;
    m->flags_out = (m->operands[0] & bit) ? 0 : PSW_Z;
    switch (m->opcode & 0x000F) {
    case 0: m->operands[0] |= bit; break;
    case 2: m->operands[0] &= ~bit; break;
    }
    if ((m->opcode & 0x000F) != 1) {
        if (m->hint & H_TI)
            nx8_write(m, ((uint32_t)m->dsr << 16) | src_index, (uint8_t)m->operands[0]);
        else
            m->r[src_index] = (uint8_t)m->operands[0];
    }
}

static void op_extbw(Nx8Machine *m)
{
    size_t index = (m->opcode & 0x00E0) >> 4;
    m->operands[0] = (m->r[index] & 0x80) ? 0xFF : 0x00;
    m->r[index + 1] = (uint8_t)m->operands[0];
    zs_check(m);
}

static void op_mul(Nx8Machine *m)
{
    m->operands[0] &= 0xFF;
    m->operands[0] *= m->operands[1];
    m->flags_changed |= PSW_Z;
    m->flags_out = m->operands[0] ? 0 : PSW_Z;
}

static void op_div(Nx8Machine *m)
{
    m->flags_changed |= PSW_Z | PSW_C;
    if (!m->operands[1]) {
        m->flags_out |= PSW_C;
        return;
    }
    uint16_t q = (uint16_t)(m->operands[0] / m->operands[1]);
    uint16_t r = (uint16_t)(m->operands[0] % m->operands[1]);
    m->operands[0] = q;
    if (m->operands[0])
        m->flags_out &= (uint8_t)~PSW_Z;
    m->r[(m->opcode >> 4) & 0x000F] = (uint8_t)r;
}

static void op_inc_ea(Nx8Machine *m)
{
    m->operands[0] = nx8_read(m, ((uint32_t)m->dsr << 16) | m->ea);
    m->operands[1] = 1;
    op_add(m);
    m->flags_changed &= (uint8_t)~PSW_C;
    nx8_write(m, ((uint32_t)m->dsr << 16) | m->ea, (uint8_t)m->operands[0]);
}

static void op_dec_ea(Nx8Machine *m)
{
    m->operands[0] = nx8_read(m, ((uint32_t)m->dsr << 16) | m->ea);
    m->operands[1] = 1;
    op_sub(m);
    m->flags_changed &= (uint8_t)~PSW_C;
    nx8_write(m, ((uint32_t)m->dsr << 16) | m->ea, (uint8_t)m->operands[0]);
}

static void op_psw_or(Nx8Machine *m) { m->psw |= (uint8_t)(m->opcode & 0xFF); }
static void op_psw_and(Nx8Machine *m) { m->psw &= (uint8_t)(m->opcode & 0xFF); }
static void op_cplc(Nx8Machine *m) { m->psw ^= PSW_C; }

static void op_bc(Nx8Machine *m)
{
    int c = (m->flags_in & PSW_C) != 0;
    int z = (m->flags_in & PSW_Z) != 0;
    int s = (m->flags_in & PSW_S) != 0;
    int ov = (m->flags_in & PSW_OV) != 0;
    int le = z | c;
    int lts = ov ^ s;
    int les = lts | z;
    int branch;
    switch ((m->opcode >> 8) & 0x000F) {
    case 0: branch = !c; break;
    case 1: branch = c; break;
    case 2: branch = !le; break;
    case 3: branch = le; break;
    case 4: branch = !lts; break;
    case 5: branch = lts; break;
    case 6: branch = !les; break;
    case 7: branch = les; break;
    case 8: branch = !z; break;
    case 9: branch = z; break;
    case 10: branch = !ov; break;
    case 11: branch = ov; break;
    case 12: branch = !s; break;
    case 13: branch = s; break;
    default: branch = 1; break;
    }
    if (branch) {
        int64_t off = (int64_t)m->operands[0];
        if (off & 0x80) off |= ~0xFFll;
        m->pc = (uint16_t)(m->pc + off * 2);
    }
}

static void op_b(Nx8Machine *m)
{
    if (m->hint & H_TI) {
        m->csr = (uint16_t)m->operands[0];
        m->pc = m->imm16;
    } else {
        m->pc = (uint16_t)m->operands[1];
    }
}

static void op_bl(Nx8Machine *m)
{
    m->lr = m->pc;
    m->lcsr = m->csr;
    op_b(m);
}

static void op_rt(Nx8Machine *m)
{
    m->csr = m->lcsr;
    m->pc = m->lr;
}

static void op_rti(Nx8Machine *m)
{
    m->csr = m->ecsr[m->psw & PSW_ELEVEL];
    m->pc = m->elr[m->psw & PSW_ELEVEL];
    m->psw = m->epsw[m->psw & PSW_ELEVEL];
}

static void op_dsr(Nx8Machine *m)
{
    if (m->hint & H_DW)
        m->last_dsr = (uint8_t)m->operands[0];
    m->dsr = m->last_dsr;
}

static void op_nop(Nx8Machine *m) { (void)m; }
static void op_swi(Nx8Machine *m) { (void)m; }
static void op_brk(Nx8Machine *m) { (void)m; }

/* ------------------------------------------------------------------ */
/* dispatch                                                            */
/* ------------------------------------------------------------------ */
static uint16_t fetch(Nx8Machine *m)
{
    if (m->csr & ~0x000F) {
        m->csr &= 0x000F;
    }
    if (m->pc & 1)
        m->pc &= (uint16_t)~1;
    uint16_t v = nx8_read_code(m, ((uint32_t)m->csr << 16) | m->pc);
    m->pc = (uint16_t)(m->pc + 2);
    return v;
}

void nx8_step(Nx8Machine *m)
{
    m->dsr = 0;
    for (;;) {
        m->opcode = fetch(m);
        const OpDesc *d = dispatch[m->opcode];
        if (!d) {
            m->faulted = 2;
            m->fault_msg = "unrecognized instruction";
            return;
        }
        m->imm16 = 0;
        if (d->has_imm)
            m->imm16 = fetch(m);

        for (size_t i = 0; i < 2; ++i) {
            uint64_t value = (m->opcode >> d->ops[i].shift) & (uint16_t)d->ops[i].mask;
            m->operand_regindex[i] = (size_t)value;
            m->operand_regsize[i] = d->ops[i].regsize;
            if (d->ops[i].regsize) {
                uint64_t v = 0;
                for (size_t b = 0; b < d->ops[i].regsize; ++b)
                    v |= (uint64_t)m->r[m->operand_regindex[i] + b] << (b * 8);
                value = v;
            }
            m->operands[i] = value;
        }
        m->hint = d->hint;

        m->flags_changed = 0;
        m->flags_in = m->psw;
        m->flags_out = PSW_Z;

        switch (d->handler) {
        case H_ADD:   op_add(m); break;
        case H_ADD16: op_add16(m); break;
        case H_ADDC:  op_addc(m); break;
        case H_AND:   op_and(m); break;
        case H_CMP:   (m->operand_regsize[0] == 2) ? op_cmp16(m) : op_sub(m); break;
        case H_CMPC:  op_subc(m); break;
        case H_MOV:   (m->operand_regsize[1] == 2) ? op_mov16(m) : op_mov(m); break;
        case H_MOV16: op_mov16(m); break;
        case H_OR:    op_or(m); break;
        case H_XOR:   op_xor(m); break;
        case H_SUB:   op_sub(m); break;
        case H_SUBC:  op_subc(m); break;
        case H_SLL:   m->shift_buffer = 0; shift_left8(m); break;
        case H_SLLC:  m->shift_buffer = m->r[(m->operand_regindex[0] - 1) & 15]; shift_left8(m); break;
        case H_SRA: {
            size_t n = m->operands[1] & 7;
            uint64_t msb = m->operands[0] & 0x80;
            m->shift_buffer = 0;
            shift_right8(m);
            if (msb) m->operands[0] |= ((0xFF >> n) ^ 0xFF);
            break;
        }
        case H_SRL:   m->shift_buffer = 0; shift_right8(m); break;
        case H_SRLC:  m->shift_buffer = m->r[(m->operand_regindex[0] + 1) & 15]; shift_right8(m); break;
        case H_LS_EA: load_store(m, m->ea, m->hint >> 8); break;
        case H_LS_R:  load_store(m, (uint16_t)m->operands[1], m->hint >> 8); break;
        case H_LS_I_R: load_store(m, (uint16_t)(m->operands[1] + m->imm16), m->hint >> 8); break;
        case H_LS_BP: {
            m->operands[1] |= (m->operands[1] & 0x20) ? 0xFFC0 : 0;
            m->operands[1] += m->r[12] | ((uint16_t)m->r[13] << 8);
            load_store(m, (uint16_t)m->operands[1], m->hint >> 8);
            break;
        }
        case H_LS_FP: {
            m->operands[1] |= (m->operands[1] & 0x20) ? 0xFFC0 : 0;
            m->operands[1] += m->r[14] | ((uint16_t)m->r[15] << 8);
            load_store(m, (uint16_t)m->operands[1], m->hint >> 8);
            break;
        }
        case H_LS_I:  load_store(m, m->imm16, m->hint >> 8); break;
        case H_ADDSP: op_addsp(m); break;
        case H_CTRL:  op_ctrl(m); break;
        case H_PUSH: {
            size_t push_size = m->operand_regsize[1] ? m->operand_regsize[1] : 2;
            if (push_size == 1) push_size = 2;
            m->sp = (uint16_t)(m->sp - push_size);
            for (size_t ix = m->operand_regsize[1]; ix-- > 0; )
                nx8_write(m, m->sp + ix, (uint8_t)(m->operands[1] >> (8 * ix)));
            break;
        }
        case H_PUSHL: {
            uint64_t mask = m->operands[1];
            if (mask & 2) {
                push16(m, m->ecsr[m->psw & PSW_ELEVEL]);
                push16(m, m->elr[m->psw & PSW_ELEVEL]);
            }
            if (mask & 4) push16(m, m->epsw[m->psw & PSW_ELEVEL]);
            if (mask & 8) {
                push16(m, m->lcsr);
                push16(m, m->lr);
            }
            if (mask & 1) push16(m, m->ea);
            break;
        }
        case H_POP: {
            size_t pop_size = m->operand_regsize[0] ? m->operand_regsize[0] : 2;
            if (pop_size == 1) pop_size = 2;
            uint64_t v = 0;
            for (size_t ix = 0; ix < m->operand_regsize[0]; ++ix)
                v |= (uint64_t)nx8_read(m, m->sp + ix) << (8 * ix);
            m->operands[0] = v;
            m->sp = (uint16_t)(m->sp + pop_size);
            break;
        }
        case H_POPL: {
            uint64_t mask = m->operands[0];
            if (mask & 1) m->ea = pop16(m);
            if (mask & 8) {
                m->lr = pop16(m);
                m->lcsr = (uint16_t)(pop16(m) & 0x000F);
            }
            if (mask & 4) m->psw = (uint8_t)pop16(m);
            if (mask & 2) {
                m->pc = pop16(m);
                m->csr = (uint16_t)(pop16(m) & 0x000F);
            }
            break;
        }
        case H_CR_R: {
            size_t op0 = (m->opcode >> 8) & 0x000F;
            size_t op1 = (m->opcode >> 4) & 0x000F;
            if (m->hint & H_ST) m->r[op0] = (uint8_t)m->cr[op1];
            else m->cr[op0] = m->r[op1];
            break;
        }
        case H_CR_EA: {
            size_t op0 = (m->opcode >> 8) & 0x000F;
            size_t length = m->opcode >> 8;
            if (m->hint & H_ST) {
                for (size_t ix = length - 1; ix != (size_t)-1; --ix)
                    nx8_write(m, ((uint32_t)m->dsr << 16) | (uint16_t)(m->ea + ix), (uint8_t)(m->cr[op0 + ix]));
            } else {
                for (size_t ix = 0; ix < length; ++ix)
                    m->cr[op0 + ix] = nx8_read(m, ((uint32_t)m->dsr << 16) | (uint16_t)(m->ea + ix));
            }
            if (m->hint & H_IA) bump_ea(m, length);
            break;
        }
        case H_LEA:   op_lea(m); break;
        case H_DAA:   op_daa(m); break;
        case H_DAS:   op_das(m); break;
        case H_NEG:   op_neg(m); break;
        case H_BITMOD: op_bitmod(m); break;
        case H_PSW_OR: op_psw_or(m); break;
        case H_PSW_AND: op_psw_and(m); break;
        case H_CPLC:  op_cplc(m); break;
        case H_BC:    op_bc(m); break;
        case H_EXTBW: op_extbw(m); break;
        case H_SWI:   op_swi(m); break;
        case H_BRK:   op_brk(m); break;
        case H_B:     op_b(m); break;
        case H_BL:    op_bl(m); break;
        case H_MUL:   op_mul(m); break;
        case H_DIV:   op_div(m); break;
        case H_INC_EA: op_inc_ea(m); break;
        case H_DEC_EA: op_dec_ea(m); break;
        case H_RT:    op_rt(m); break;
        case H_RTI:   op_rti(m); break;
        case H_NOP:   op_nop(m); break;
        case H_DSR:   op_dsr(m); break;
        default: break;
        }

        m->psw = (uint8_t)(m->psw & ~m->flags_changed);
        m->psw |= (uint8_t)(m->flags_out & m->flags_changed);

        if ((m->hint & H_WB) && m->operand_regsize[0])
            for (size_t b = 0; b < m->operand_regsize[0]; ++b)
                m->r[m->operand_regindex[0] + b] = (uint8_t)(m->operands[0] >> (b * 8));

        m->steps++;
        if (!(m->hint & H_DS))
            return;
    }
}

size_t nx8_call(Nx8Machine *m, uint32_t addr, uint64_t max_steps)
{
    uint64_t start = m->steps;
    m->lr = NX8_SENTINEL & 0xFFFF;
    m->lcsr = 0;
    m->csr = (uint16_t)((addr >> 16) & 0x000F);
    m->pc = (uint16_t)addr;
    while (m->pc != (NX8_SENTINEL & 0xFFFF) || m->csr != 0) {
        if (m->faulted)
            return (size_t)-1;
        nx8_step(m);
        if (m->faulted)
            return (size_t)-1;
        if (max_steps && m->steps - start > max_steps)
            return (size_t)-1;
    }
    return (size_t)(m->steps - start);
}

const char *nx8_disasm(const Nx8Machine *m, uint32_t addr, char *buf, size_t buflen)
{
    (void)m;
    (void)addr;
    snprintf(buf, buflen, "%05X", addr);
    return buf;
}
