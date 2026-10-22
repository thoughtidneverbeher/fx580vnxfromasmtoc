"""
Generate emu/opcode_table.inc from tools/nx8.py's instruction table.

One source of truth: the Python decoder (validated against the Lua
disassembler over the whole ROM) drives both the analysis tools and the C
emulator, so a change to an encoding can never silently desynchronise them.
The hint bits come straight from CasioEmu's CPU.cpp opcode_sources table.

Usage: python3 tools/gen_opcode_table.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nx8

OUT = os.path.join(os.path.dirname(HERE), "emu", "opcode_table.inc")

# operand format codes; must match nx8.c
FMT_CODES = {
    "im": "F_IM", "bo": "F_BO", "jo": "F_JO",
    "r": "F_R", "er": "F_ER", "xr": "F_XR", "qr": "F_QR",
    "cr": "F_CR", "cer": "F_CER", "cxr": "F_CXR", "cqr": "F_CQR",
    "dsr": "F_DSR", "ea": "F_EA", "eap": "F_EAP",
    "elr": "F_ELR", "lr": "F_LR", "psw": "F_PSW", "epsw": "F_EPSW",
    "ecsr": "F_ECSR", "sp": "F_SP", "pc": "F_PC",
    "co": "F_CO", "lab": "F_LAB", "dlab": "F_DLAB",
    "push": "F_PUSH", "pop": "F_POP",
}

MNEMONIC_TO_HANDLER = {
    "add": "ADD", "addc": "ADDC", "and": "AND", "cmp": "CMP", "cmpc": "CMPC",
    "mov": "MOV", "or": "OR", "xor": "XOR", "sub": "SUB", "subc": "SUBC",
    "sll": "SLL", "sllc": "SLLC", "sra": "SRA", "srl": "SRL", "srlc": "SRLC",
    "daa": "DAA", "das": "DAS", "neg": "NEG",
    "sb": "BITMOD", "rb": "BITMOD", "tb": "BITMOD",
    "ei": "PSW_OR", "di": "PSW_AND", "sc": "PSW_OR", "rc": "PSW_AND",
    "clpc": "CPLC", "bc": "BC", "extbw": "EXTBW", "swi": "SWI", "brk": "BRK",
    "b": "B", "bl": "BL", "mul": "MUL", "div": "DIV",
    "inc": "INC_EA", "dec": "DEC_EA", "rt": "RT", "rti": "RTI",
    "nop": "NOP", "dsr": "DSR", "push": "PUSH", "pop": "POP",
}

# A few spellings map to different handlers per encoding (load/store variants,
# control-register moves and the two `mov er, imm` forms).
SPECIFIC = {
    ("l", 0x9032): "LS_EA", ("l", 0x9052): "LS_EA", ("l", 0x9002): "LS_R",
    ("l", 0xA008): "LS_I_R", ("l", 0xB000): "LS_BP", ("l", 0xB040): "LS_FP",
    ("l", 0x9012): "LS_I", ("l", 0x9030): "LS_EA", ("l", 0x9050): "LS_EA",
    ("l", 0x9000): "LS_R", ("l", 0x9008): "LS_I_R", ("l", 0xD000): "LS_BP",
    ("l", 0xD040): "LS_FP", ("l", 0x9010): "LS_I", ("l", 0x9034): "LS_EA",
    ("l", 0x9054): "LS_EA", ("l", 0x9036): "LS_EA", ("l", 0x9056): "LS_EA",
    ("st", 0x9033): "LS_EA", ("st", 0x9053): "LS_EA", ("st", 0x9003): "LS_R",
    ("st", 0xA009): "LS_I_R", ("st", 0xB080): "LS_BP", ("st", 0xB0C0): "LS_FP",
    ("st", 0x9013): "LS_I", ("st", 0x9031): "LS_EA", ("st", 0x9051): "LS_EA",
    ("st", 0x9001): "LS_R", ("st", 0x9009): "LS_I_R", ("st", 0xD080): "LS_BP",
    ("st", 0xD0C0): "LS_FP", ("st", 0x9011): "LS_I", ("st", 0x9035): "LS_EA",
    ("st", 0x9055): "LS_EA", ("st", 0x9037): "LS_EA", ("st", 0x9057): "LS_EA",
    ("lea", 0xF00A): "LEA", ("lea", 0xF00B): "LEA", ("lea", 0xF00C): "LEA",
    ("mov", 0xF02D): "CR_EA", ("mov", 0xF03D): "CR_EA", ("mov", 0xF00D): "CR_EA",
    ("mov", 0xF01D): "CR_EA", ("mov", 0xF04D): "CR_EA", ("mov", 0xF05D): "CR_EA",
    ("mov", 0xF06D): "CR_EA", ("mov", 0xF07D): "CR_EA",
    ("mov", 0xF0AD): "CR_EA", ("mov", 0xF0BD): "CR_EA", ("mov", 0xF08D): "CR_EA",
    ("mov", 0xF09D): "CR_EA", ("mov", 0xF0CD): "CR_EA", ("mov", 0xF0DD): "CR_EA",
    ("mov", 0xF0ED): "CR_EA", ("mov", 0xF0FD): "CR_EA",
    ("mov", 0xA00E): "CR_R", ("mov", 0xA006): "CR_R",
    ("mov", 0xA00F): "CTRL", ("mov", 0xA00D): "CTRL", ("mov", 0xA00C): "CTRL",
    ("mov", 0xA005): "CTRL", ("mov", 0xA01A): "CTRL", ("mov", 0xA00B): "CTRL",
    ("mov", 0xA007): "CTRL", ("mov", 0xA004): "CTRL", ("mov", 0xA003): "CTRL",
    ("mov", 0xA10A): "CTRL", ("mov", 0xE900): "CTRL",
    ("add", 0xE100): "ADDSP",
    ("mov", 0xE000): "MOV16", ("mov", 0xF005): "MOV16",
    ("add", 0xF006): "ADD16", ("add", 0xE080): "ADD16",
    ("mov", 0xF000): "B", ("mov", 0xF001): "BL",
    ("push", 0xF0CE): "PUSHL", ("pop", 0xF08E): "POPL",
}

LEN = {"r": 1, "er": 2, "xr": 4, "qr": 8}

# ---------------------------------------------------------------------------
# Operand layouts for *execution*. The disassembler's table is shaped for
# printing (a `bc` shows condition + offset, a `lea er` shows one operand), so
# for the encodings where the two tables disagree we emit CasioEmu's CPU.cpp
# layout instead. Each entry: list of up to 3 (fmt, shift, mask, regsize).
# ---------------------------------------------------------------------------
N = ("F_NONE", 0, 0, 0)
IM8 = ("F_IM", 0, 0xFF, 0)
IM6 = ("F_IM", 0, 0x3F, 0)
ER4 = ("F_ER", 4, 14, 2)
CSR_IMM8 = ("F_IM", 8, 15, 0)
PUSH_MASK = ("F_IM", 8, 15, 0)

CPU_LAYOUT = {
    # conditional branches: CPU sees one operand, the whole low byte
    ("bc", 0xC000): [IM8, N, N],
    # register-indirect branches: the target address is operand 1
    ("b", 0xF002): [N, ER4, N],
    ("bl", 0xF003): [N, ER4, N],
    # absolute branches: operand 0 is the code segment register
    ("b", 0xF000): [CSR_IMM8, N, N],
    ("bl", 0xF001): [CSR_IMM8, N, N],
    # stack pointer adjust: the signed displacement is operand 0
    ("add", 0xE100): [IM8, N, N],
    # lea: operand 1 carries the base register
    ("lea", 0xF00A): [N, ER4, N],
    ("lea", 0xF00B): [N, ER4, N],
    ("lea", 0xF00C): [N, N, N],
    # base-pointer/frame-pointer addressing: operand 1 is the 6-bit offset
    ("l", 0xB000): [("F_ER", 8, 14, 2), IM6, N],
    ("l", 0xB040): [("F_ER", 8, 14, 2), IM6, N],
    ("l", 0xD000): [("F_ER", 8, 15, 1), IM6, N],
    ("l", 0xD040): [("F_ER", 8, 15, 1), IM6, N],
    ("st", 0xB080): [("F_ER", 8, 14, 2), IM6, N],
    ("st", 0xB0C0): [("F_ER", 8, 14, 2), IM6, N],
    ("st", 0xD080): [("F_ER", 8, 15, 1), IM6, N],
    ("st", 0xD0C0): [("F_ER", 8, 15, 1), IM6, N],
    # push: the pushed value is operand 1 (pushl keeps its mask there too)
    ("push", 0xF05E): [N, ("F_ER", 8, 14, 2), N],
    ("push", 0xF07E): [N, ("F_QR", 8, 8, 8), N],
    ("push", 0xF04E): [N, ("F_R", 8, 15, 1), N],
    ("push", 0xF06E): [N, ("F_XR", 8, 12, 4), N],
    ("push", 0xF0CE): [N, PUSH_MASK, N],
}


def ls_hint(mnemonic, opmask, ops, fmt):
    length = LEN[fmt] << 8
    st = 0 if mnemonic == "l" else 0x0002
    # Only the [ea+] (`eap`) operand forms post-increment; [ea], [d_xxxx],
    # [er], [offset+er] and bp/fp-relative forms leave ea untouched.
    ia = 0x0010 if any(f == "eap" for (f, shift, mask) in ops) else 0
    ti = 0x0020 if opmask in (0xA008, 0xA009, 0x9008, 0x9009) else 0
    return length | st | ia | ti


def build_hints():
    hints = {}
    for spec in nx8.SOURCE:
        s = tuple(spec) + (0,) * (5 - len(spec))
        if isinstance(s[0], tuple):
            s = (s[0][0], list(s[0][1:])) + tuple(s[1:])
        s = s[:5]
        mn, ops, om, has_imm, off = s
        if mn in ("l", "st"):
            fmt = None
            for (f, shift, mask) in ops:
                if shift == 8:
                    fmt = f
            hints[(mn, om)] = ls_hint(mn, om, ops, fmt)
            continue
        # arithmetic/shift/alu/pop: CasioEmu marks these H_WB
        wb = mn in ("add", "addc", "and", "mov", "or", "xor", "sub", "subc",
                    "sll", "sllc", "sra", "srl", "srlc", "daa", "das", "neg",
                    "mul", "div", "pop")
        # `pop <mask>` writes pc/psw/lr/ea inside the handler; no register
        # writeback happens through H_WB for it.
        if (mn, om) == ("pop", 0xF08E):
            wb = False
        ie = 0x0001 if om in (0xE000, 0xE080) else 0
        ti = 0x0020 if (mn in ("b", "bl", "lea", "sb", "tb", "rb") and wb is False
                        and om in (0xF000, 0xF001, 0xF00B, 0xF00C, 0xA080, 0xA081,
                                   0xA082)) else 0
        hints[(mn, om)] = (0x0040 if wb else 0) | ie | ti
    # control-register family, keyed by opcode (one handler, several cases)
    hints.update({
        ("mov", 0xA00F): 1 << 8,
        ("mov", 0xA00D): 2 << 8,
        ("mov", 0xA00C): 3 << 8,
        ("mov", 0xA005): 0x0040 | 4 << 8,
        ("mov", 0xA01A): 0x0040 | 5 << 8,
        ("mov", 0xA00B): 6 << 8,
        ("mov", 0xE900): 7 << 8,
        ("mov", 0xA007): 0x0040 | 8 << 8,
        ("mov", 0xA004): 0x0040 | 9 << 8,
        ("mov", 0xA003): 0x0040 | 10 << 8,
        ("mov", 0xA10A): 11 << 8,
    })
    for opmask, length in ((0xF00D, 1), (0xF01D, 1), (0xF02D, 2), (0xF03D, 2),
                           (0xF04D, 4), (0xF05D, 4), (0xF06D, 8), (0xF07D, 8)):
        hints[("mov", opmask)] = (length << 8) | (0x0010 if opmask & 0x10 else 0)
    for opmask, length in ((0xF08D, 1), (0xF09D, 1), (0xF0AD, 2), (0xF0BD, 2),
                           (0xF0CD, 4), (0xF0DD, 4), (0xF0ED, 8), (0xF0FD, 8)):
        hints[("mov", opmask)] = (length << 8) | 0x0002 | (0x0010 if opmask & 0x10 else 0)
    # dsr prefix instructions
    hints[("dsr", 0xFE9F)] = 0x0008
    hints[("dsr", 0xE300)] = 0x0008 | 0x0004
    hints[("dsr", 0x900F)] = 0x0008 | 0x0004
    return hints


def main():
    hints = build_hints()
    rows = []
    seen = set()
    for spec in nx8.SOURCE:
        s = tuple(spec) + (0,) * (5 - len(spec))
        if isinstance(s[0], tuple):
            s = (s[0][0], list(s[0][1:])) + tuple(s[1:])
        if len(s) > 5:
            s = s[:5]
        mnemonic, operands, opmask, has_imm, offsetable = s
        handler = SPECIFIC.get((mnemonic, opmask)) or MNEMONIC_TO_HANDLER.get(mnemonic)
        if handler is None:
            raise RuntimeError("no handler for %s/%04X" % (mnemonic, opmask))
        if (mnemonic, opmask) in seen:
            raise RuntimeError("duplicate table entry %s/%04X" % (mnemonic, opmask))
        seen.add((mnemonic, opmask))
        # Only operand fields inside the 16-bit opcode word vary the encoding;
        # the 16-bit immediate that follows is not part of the dispatch key
        # (CasioEmu uses uint16_t varying_bits, so shift-16 operands drop out).
        varying = 0
        for (_, shift, mask) in operands:
            if 0 <= shift < 16:
                varying |= abs(mask) << shift
        ops = []
        override = CPU_LAYOUT.get((mnemonic, opmask))
        if override is not None:
            for (code, shift, mask, regsize) in override:
                ops.append((code, shift, mask, regsize))
        else:
            for (fmt, shift, mask) in operands:
                if shift == 16:
                    shift, mask = 0, 0xFFFF
                code = FMT_CODES[fmt]
                regsize = {"r": 1, "er": 2, "xr": 4, "qr": 8}.get(fmt, 0)
                # Sign extension of negative masks happens in the handlers
                # (ADD16/MOV16/LS_BP/LS_FP), exactly like CasioEmu: store width.
                ops.append((code, shift, abs(mask), regsize))
        while len(ops) < 3:
            ops.append(("F_NONE", 0, 0, 0))
        hint = hints.get((mnemonic, opmask), 0)
        rows.append("    { 0x%04X, 0x%04X, 0x%04X, H_%s, %d, %d, {\n" %
                    (opmask, varying & 0xFFFF, hint, handler, 1 if has_imm else 0, offsetable))
        for (code, shift, mask, regsize) in ops:
            rows.append("      { %s, %d, %d, %d },\n" % (code, shift, mask, regsize))
        rows.append("    } },\n")

    with open(OUT, "w") as f:
        f.write("/* Generated by tools/gen_opcode_table.py from tools/nx8.py. Do not edit. */\n")
        f.write("/* fields: opcode mask, varying bits, CPU hints, handler, */\n")
        f.write("/* has 16-bit immediate, addressable operand, operands.   */\n")
        f.write("static const OpDesc opcode_sources[] = {\n")
        for r in rows:
            f.write(r)
        f.write("};\n")
        f.write("#define OPCODE_SOURCE_COUNT (sizeof(opcode_sources) / sizeof(opcode_sources[0]))\n")
    print("wrote %s (%d entries)" % (os.path.relpath(OUT), len(rows)))


if __name__ == "__main__":
    main()
