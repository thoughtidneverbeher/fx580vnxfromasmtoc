"""
nX-U8/100 instruction set, ported to Python from two authoritative sources:

  1. machine/disassembler/disassembler.lua      (instruction table + operand printing)
  2. machine/emulator/src/Chipset/CPU*.cpp      (exact semantics of each instruction)

This module is the single source of truth for *decoding*. Anything that needs to
read ROM code (analysis, CFG building, decompilation, tests) must go through here
so that there is exactly one place where an encoding mistake can hide.

Register model (see CPU.hpp):
    r0..r15      8-bit registers, contiguous in one file
    er / xr / qr pairs/quads/octets of that file: erN = (rN low, rN+1 high)
    er16 does not exist; "er14"/"er8" are just aliases of adjacent bytes.
"""

COND = ["ge", "lt", "gt", "le", "ges", "lts", "gts", "les", "ne", "eq", "nv", "ov", "ps", "ns", "al", "al"]
# NOTE: the emulator treats condition 15 (0xF, unconditional) exactly like 14 ("al").
# disassembler.lua only lists 15 names, so 0xF00-family opcodes decode to '?' there.

# ---------------------------------------------------------------------------
# Instruction table.
# Each entry: (mnemonic, [(fmt, shift, mask), ...], opmask, has_imm16, offsetable)
#   fmt        how the operand is printed ('r', 'er', 'im', ...)
#   shift/mask bit position of the operand field inside the 16-bit opcode
#             (shift == 16 means "taken from the following word", shift < 0 means
#              "constant operand", negative mask means sign-extended)
#   opmask     fixed bits of the opcode; remaining bits are operand fields
#   has_imm16  instruction is 4 bytes: a 16-bit immediate word follows
#   offsetable 1-based index (1-based in Lua: ix) of the operand that is the
#              *address* operand; the disassembler wraps it in [ ] and folds a
#              following immediate into it (e.g. "l r0, -1[er14]")
# ---------------------------------------------------------------------------
SOURCE = [
    (("add",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8001, False, 0),
    (("add",  ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x1000, False, 0),
    (("add",  ("er", 8, 0x000E), ("er", 4, 0x000E)), 0xF006, False, 0),
    (("add",  ("er", 8, 0x000E), ("im", 0, -0x007F)), 0xE080, False, 0),
    (("addc", ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8006, False, 0),
    (("addc", ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x6000, False, 0),
    (("and",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8002, False, 0),
    (("and",  ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x2000, False, 0),
    (("cmp",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8007, False, 0),
    (("cmp",  ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x7000, False, 0),
    (("cmpc", ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8005, False, 0),
    (("cmpc", ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x5000, False, 0),
    (("mov",  ("er", 8, 0x000E), ("er", 4, 0x000E)), 0xF005, False, 0),
    (("mov",  ("er", 8, 0x000E), ("im", 0, -0x007F)), 0xE000, False, 0),
    (("mov",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8000, False, 0),
    (("mov",  ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x0000, False, 0),
    (("or",   ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8003, False, 0),
    (("or",   ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x3000, False, 0),
    (("xor",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8004, False, 0),
    (("xor",  ("r", 8, 0x000F), ("im", 0, 0x00FF)), 0x4000, False, 0),
    (("cmp",  ("er", 8, 0x000E), ("er", 4, 0x000E)), 0xF007, False, 0),
    (("sub",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8008, False, 0),
    (("subc", ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x8009, False, 0),
    (("sll",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x800A, False, 0),
    (("sll",  ("r", 8, 0x000F), ("im", 4, 0x0007)), 0x900A, False, 0),
    (("sllc", ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x800B, False, 0),
    (("sllc", ("r", 8, 0x000F), ("im", 4, 0x0007)), 0x900B, False, 0),
    (("sra",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x800E, False, 0),
    (("sra",  ("r", 8, 0x000F), ("im", 4, 0x0007)), 0x900E, False, 0),
    (("srl",  ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x800C, False, 0),
    (("srl",  ("r", 8, 0x000F), ("im", 4, 0x0007)), 0x900C, False, 0),
    (("srlc", ("r", 8, 0x000F), ("r", 4, 0x000F)), 0x800D, False, 0),
    (("srlc", ("r", 8, 0x000F), ("im", 4, 0x0007)), 0x900D, False, 0),
    (("l",    ("er", 8, 0x000E), ("ea", 0, 0x0000)), 0x9032, False, 2),
    (("l",    ("er", 8, 0x000E), ("eap", 0, 0x0000)), 0x9052, False, 2),
    (("l",    ("er", 8, 0x000E), ("er", 4, 0x000E)), 0x9002, False, 2),
    (("l",    ("er", 8, 0x000E), ("er", 4, 0x000E), ("im", 16, 0xFFFF)), 0xA008, True, 2),
    (("l",    ("er", 8, 0x000E), ("er", -1, 0x000C), ("im", 0, -0x003F)), 0xB000, False, 2),
    (("l",    ("er", 8, 0x000E), ("er", -1, 0x000E), ("im", 0, -0x003F)), 0xB040, False, 2),
    (("l",    ("er", 8, 0x000E), ("im", 16, 0xFFFF)), 0x9012, True, 2),
    (("l",    ("r", 8, 0x000F), ("ea", 0, 0x0000)), 0x9030, False, 2),
    (("l",    ("r", 8, 0x000F), ("eap", 0, 0x0000)), 0x9050, False, 2),
    (("l",    ("r", 8, 0x000F), ("er", 4, 0x000E)), 0x9000, False, 2),
    (("l",    ("r", 8, 0x000F), ("er", 4, 0x000E), ("im", 16, 0xFFFF)), 0x9008, True, 2),
    (("l",    ("r", 8, 0x000F), ("er", -1, 0x000C), ("im", 0, -0x003F)), 0xD000, False, 2),
    (("l",    ("r", 8, 0x000F), ("er", -1, 0x000E), ("im", 0, -0x003F)), 0xD040, False, 2),
    (("l",    ("r", 8, 0x000F), ("im", 16, 0xFFFF)), 0x9010, True, 2),
    (("l",    ("xr", 8, 0x000C), ("ea", 0, 0x0000)), 0x9034, False, 2),
    (("l",    ("xr", 8, 0x000C), ("eap", 0, 0x0000)), 0x9054, False, 2),
    (("l",    ("qr", 8, 0x0008), ("ea", 0, 0x0000)), 0x9036, False, 2),
    (("l",    ("qr", 8, 0x0008), ("eap", 0, 0x0000)), 0x9056, False, 2),
    (("st",   ("er", 8, 0x000E), ("ea", 0, 0x0000)), 0x9033, False, 2),
    (("st",   ("er", 8, 0x000E), ("eap", 0, 0x0000)), 0x9053, False, 2),
    (("st",   ("er", 8, 0x000E), ("er", 4, 0x000E)), 0x9003, False, 2),
    (("st",   ("er", 8, 0x000E), ("er", 4, 0x000E), ("im", 16, 0xFFFF)), 0xA009, True, 2),
    (("st",   ("er", 8, 0x000E), ("er", -1, 0x000C), ("im", 0, -0x003F)), 0xB080, False, 2),
    (("st",   ("er", 8, 0x000E), ("er", -1, 0x000E), ("im", 0, -0x003F)), 0xB0C0, False, 2),
    (("st",   ("er", 8, 0x000E), ("im", 16, 0xFFFF)), 0x9013, True, 2),
    (("st",   ("r", 8, 0x000F), ("ea", 0, 0x0000)), 0x9031, False, 2),
    (("st",   ("r", 8, 0x000F), ("eap", 0, 0x0000)), 0x9051, False, 2),
    (("st",   ("r", 8, 0x000F), ("er", 4, 0x000E)), 0x9001, False, 2),
    (("st",   ("r", 8, 0x000F), ("er", 4, 0x000E), ("im", 16, 0xFFFF)), 0x9009, True, 2),
    (("st",   ("r", 8, 0x000F), ("er", -1, 0x000C), ("im", 0, -0x003F)), 0xD080, False, 2),
    (("st",   ("r", 8, 0x000F), ("er", -1, 0x000E), ("im", 0, -0x003F)), 0xD0C0, False, 2),
    (("st",   ("r", 8, 0x000F), ("im", 16, 0xFFFF)), 0x9011, True, 2),
    (("st",   ("xr", 8, 0x000C), ("ea", 0, 0x0000)), 0x9035, False, 2),
    (("st",   ("xr", 8, 0x000C), ("eap", 0, 0x0000)), 0x9055, False, 2),
    (("st",   ("qr", 8, 0x0008), ("ea", 0, 0x0000)), 0x9037, False, 2),
    (("st",   ("qr", 8, 0x0008), ("eap", 0, 0x0000)), 0x9057, False, 2),
    (("add",  ("sp", 0, 0x0000), ("im", 0, -0x00FF)), 0xE100, False, 0),
    (("mov",  ("ecsr", 0, 0x0000), ("r", 4, 0x000F)), 0xA00F, False, 0),
    (("mov",  ("elr", 0, 0x0000), ("er", 8, 0x000E)), 0xA00D, False, 0),
    (("mov",  ("epsw", 0, 0x0000), ("r", 4, 0x000F)), 0xA00C, False, 0),
    (("mov",  ("er", 8, 0x000E), ("elr", 0, 0x0000)), 0xA005, False, 0),
    (("mov",  ("er", 8, 0x000E), ("sp", 0, 0x0000)), 0xA01A, False, 0),
    (("mov",  ("psw", 0, 0x0000), ("r", 4, 0x000F)), 0xA00B, False, 0),
    (("mov",  ("psw", 0, 0x0000), ("im", 0, 0x00FF)), 0xE900, False, 0),
    (("mov",  ("r", 8, 0x000F), ("ecsr", 0, 0x0000)), 0xA007, False, 0),
    (("mov",  ("r", 8, 0x000F), ("epsw", 0, 0x0000)), 0xA004, False, 0),
    (("mov",  ("r", 8, 0x000F), ("psw", 0, 0x0000)), 0xA003, False, 0),
    (("mov",  ("sp", 0, 0x0000), ("er", 4, 0x000E)), 0xA10A, False, 0),
    (("push", ("er", 8, 0x000E)), 0xF05E, False, 0),
    (("push", ("qr", 8, 0x0008)), 0xF07E, False, 0),
    (("push", ("r", 8, 0x000F)), 0xF04E, False, 0),
    (("push", ("xr", 8, 0x000C)), 0xF06E, False, 0),
    (("push", ("push", 8, 0x000F)), 0xF0CE, False, 0),
    (("pop",  ("er", 8, 0x000E)), 0xF01E, False, 0),
    (("pop",  ("qr", 8, 0x0008)), 0xF03E, False, 0),
    (("pop",  ("r", 8, 0x000F)), 0xF00E, False, 0),
    (("pop",  ("xr", 8, 0x000C)), 0xF02E, False, 0),
    (("pop",  ("pop", 8, 0x000F)), 0xF08E, False, 0),
    (("mov",  ("cr", 8, 0x000F), ("r", 4, 0x000F)), 0xA00E, False, 0),
    (("mov",  ("cer", 8, 0x000E), ("ea", 0, 0x0000)), 0xF02D, False, 2),
    (("mov",  ("cer", 8, 0x000E), ("eap", 0, 0x0000)), 0xF03D, False, 2),
    (("mov",  ("cr", 8, 0x000F), ("ea", 0, 0x0000)), 0xF00D, False, 2),
    (("mov",  ("cr", 8, 0x000F), ("eap", 0, 0x0000)), 0xF01D, False, 2),
    (("mov",  ("cxr", 8, 0x000C), ("ea", 0, 0x0000)), 0xF04D, False, 2),
    (("mov",  ("cxr", 8, 0x000C), ("eap", 0, 0x0000)), 0xF05D, False, 2),
    (("mov",  ("cqr", 8, 0x000C), ("ea", 0, 0x0000)), 0xF06D, False, 2),
    (("mov",  ("cqr", 8, 0x0008), ("eap", 0, 0x0000)), 0xF07D, False, 2),
    (("mov",  ("r", 4, 0x000F), ("cr", 8, 0x000F)), 0xA006, False, 0),
    (("mov",  ("ea", 0, 0x0000), ("cer", 8, 0x000E)), 0xF0AD, False, 1),
    (("mov",  ("eap", 0, 0x0000), ("cer", 8, 0x000E)), 0xF0BD, False, 1),
    (("mov",  ("ea", 0, 0x0000), ("cr", 8, 0x000F)), 0xF08D, False, 1),
    (("mov",  ("eap", 0, 0x0000), ("cr", 8, 0x000F)), 0xF09D, False, 1),
    (("mov",  ("ea", 0, 0x0000), ("cxr", 8, 0x000C)), 0xF0CD, False, 1),
    (("mov",  ("eap", 0, 0x0000), ("cxr", 8, 0x000C)), 0xF0DD, False, 1),
    (("mov",  ("ea", 0, 0x0000), ("cqr", 8, 0x000C)), 0xF0ED, False, 1),
    (("mov",  ("eap", 0, 0x0000), ("cqr", 8, 0x0008)), 0xF0FD, False, 1),
    (("lea",  ("er", 4, 0x000E)), 0xF00A, False, 1),
    (("lea",  ("er", 4, 0x000E), ("im", 16, 0xFFFF)), 0xF00B, True, 1),
    (("lea",  ("im", 16, 0xFFFF)), 0xF00C, True, 1),
    (("daa",  ("r", 8, 0x000F)), 0x801F, False, 0),
    (("das",  ("r", 8, 0x000F)), 0x803F, False, 0),
    (("neg",  ("r", 8, 0x000F)), 0x805F, False, 0),
    (("sb",   ("r", 8, 0x000F), ("bo", 4, 0x0007)), 0xA000, False, 0),
    (("sb",   ("im", 16, 0xFFFF), ("bo", 4, 0x0007)), 0xA080, True, 1),
    (("rb",   ("r", 8, 0x000F), ("bo", 4, 0x0007)), 0xA002, False, 0),
    (("rb",   ("im", 16, 0xFFFF), ("bo", 4, 0x0007)), 0xA082, True, 1),
    (("tb",   ("r", 8, 0x000F), ("bo", 4, 0x0007)), 0xA001, False, 0),
    (("tb",   ("im", 16, 0xFFFF), ("bo", 4, 0x0007)), 0xA081, True, 1),
    (("ei",), 0xED08, False, 0),
    (("di",), 0xEBF7, False, 0),
    (("sc",), 0xED80, False, 0),
    (("rc",), 0xEB7F, False, 0),
    (("clpc",), 0xFECF, False, 0),
    (("bc",   ("co", 8, 0x000F), ("jo", 0, -0x00FF)), 0xC000, False, 0),
    (("extbw", ("er", -1, 0x0000)), 0x810F, False, 0),
    (("extbw", ("er", -1, 0x0002)), 0x832F, False, 0),
    (("extbw", ("er", -1, 0x0004)), 0x854F, False, 0),
    (("extbw", ("er", -1, 0x0006)), 0x876F, False, 0),
    (("extbw", ("er", -1, 0x0008)), 0x898F, False, 0),
    (("extbw", ("er", -1, 0x000A)), 0x8BAF, False, 0),
    (("extbw", ("er", -1, 0x000C)), 0x8DCF, False, 0),
    (("extbw", ("er", -1, 0x000E)), 0x8FEF, False, 0),
    (("swi",  ("im", 0, 0x003F)), 0xE500, False, 0),
    (("brk",), 0xFFFF, False, 0),
    (("b",    ("im", 8, 0x000F), ("im", 16, 0xFFFF)), 0xF000, True, 0),
    (("b",    ("er", 4, 0x000E)), 0xF002, False, 0),
    (("bl",   ("im", 8, 0x000F), ("im", 16, 0xFFFF)), 0xF001, True, 0),
    (("bl",   ("er", 4, 0x000E)), 0xF003, False, 0),
    (("mul",  ("er", 8, 0x000E), ("r", 4, 0x000F)), 0xF004, False, 0),
    (("div",  ("er", 8, 0x000E), ("r", 4, 0x000F)), 0xF009, False, 0),
    (("inc",  ("ea", 0, 0x0000)), 0xFE2F, False, 1),
    (("dec",  ("ea", 0, 0x0000)), 0xFE3F, False, 1),
    (("rt",), 0xFE1F, False, 0),
    (("rti",), 0xFE0F, False, 0),
    (("nop",), 0xFE8F, False, 0),
    (("dsr",  ("dsr", -1, 0xFFFF)), 0xFE9F, False, 0),
    (("dsr",  ("r", 4, 0x000F)), 0x900F, False, 0),
    (("dsr",  ("im", 0, 0x00FF)), 0xE300, False, 0),
]

# mnemonics whose "im" operands are absolute addresses and get d_XXXXX labels
DATA_ADDR_MNEMONICS = {"l", "st", "lea", "sb", "tb", "rb"}


def _sign_extend(value, mask):
    """Sign-extend `value` to the width of `mask` (mask must be odd+1 pattern)."""
    bit = mask + 1
    if bit > 1 and (value & (bit >> 1)):
        return value - bit
    return value


class Insn:
    """One decoded instruction (or None-able for unknown)."""

    __slots__ = ("addr", "length", "mnemonic", "params", "offsetable", "dsr", "has_imm", "imm16", "words")

    def __init__(self, addr, length, mnemonic, params, offsetable, has_imm, imm16, words):
        self.addr = addr
        self.length = length
        self.mnemonic = mnemonic
        self.params = params          # [(value, fmt), ...]
        self.offsetable = offsetable  # 1-based index of the address operand, 0 = none
        self.dsr = None               # (value, fmt) for a preceding dsr prefix
        self.has_imm = has_imm
        self.imm16 = imm16
        self.words = words

    def __repr__(self):
        return "<%05X %s>" % (self.addr, format_insn(self))


def _build_dispatch():
    table = {}
    for spec in SOURCE:
        spec = tuple(spec) + (0,) * (5 - len(spec))
        if isinstance(spec[0], tuple):
            body = spec[0]
            spec = (body[0], list(body[1:])) + tuple(spec[1:])
        if len(spec) > 5:
            spec = spec[:5]
        mnemonic, operands, opmask, has_imm, offsetable = spec
        stub = (mnemonic, operands, has_imm, offsetable)
        varying = 0
        for (_, shift, mask) in operands:
            if shift >= 0:
                varying |= abs(mask) << shift
        keys = [opmask]
        bit = 0x8000
        while bit:
            if varying & bit:
                keys += [k | bit for k in keys]
            bit >>= 1
        for key in keys:
            if key in table:
                raise RuntimeError("opcode clash %04X" % key)
            table[key] = stub
    return table


_DISPATCH = _build_dispatch()


def decode(rom, addr):
    """Decode one instruction at `addr` (byte address, must be even).

    Returns an `Insn`, or an Insn with mnemonic '?' for unknown opcodes (the
    emulator would PANIC there; the disassembler prints '?').
    """
    def word_at(a):
        return rom[a] | (rom[a + 1] << 8)

    opcode = word_at(addr)
    stub = _DISPATCH.get(opcode)
    if stub is None:
        return Insn(addr, 2, "?", [], 0, False, 0, [opcode])
    mnemonic, operands, has_imm, offsetable = stub
    imm16 = word_at(addr + 2) if has_imm else 0
    length = 4 if has_imm else 2
    words = [opcode] + ([imm16] if has_imm else [])

    params = []
    for (fmt, shift, mask) in operands:
        signed = mask < 0
        pmask = abs(mask)
        src = imm16 if shift == 16 else opcode
        s = 0 if shift == 16 else shift
        if shift < 0:
            value = pmask
        else:
            value = (src >> s) & pmask
        if signed:
            value = _sign_extend(value, pmask)
        params.append((value, fmt))
    return Insn(addr, length, mnemonic, params, offsetable, has_imm, imm16, words)


def format_operand(value, fmt):
    if fmt == "im" or fmt == "bo" or fmt == "jo":
        return "%d" % value
    if fmt == "eap":
        return "ea+"
    if fmt in ("r", "er", "xr", "qr", "cr", "cer", "cxr", "cqr"):
        return "%s%d" % (fmt, value)
    if fmt == "co":
        return COND[value]
    return fmt


# push/pop with a mask operand print the pushed *registers*, not the mask
PUSH_MASK_NAMES = [(2, "elr"), (4, "epsw"), (8, "lr"), (1, "ea")]
POP_MASK_NAMES = [(1, "ea"), (8, "lr"), (4, "psw"), (2, "pc")]


def expand_pushpop(insn):
    """('push lr' / 'pop lr, psw' style) or None if not a masked push/pop."""
    for (value, fmt) in insn.params:
        if fmt == "push" and insn.mnemonic == "push":
            return [name for (bit, name) in PUSH_MASK_NAMES if value & bit]
        if fmt == "pop" and insn.mnemonic == "pop":
            return [name for (bit, name) in POP_MASK_NAMES if value & bit]
    return None


def format_insn(insn, label_of=None, datalabel_of=None):
    """Render "<mnemonic> <operands>" exactly like disassembler.lua.

    label_of(addr)    -> code label name (branch targets)
    datalabel_of(addr)-> data label name (l/st address operands)
    """
    names = expand_pushpop(insn)
    if names is not None:
        return insn.mnemonic + ((" " + ", ".join(names)) if names else "")

    # b/bl with a CSR-relative absolute target: disassembler.lua keeps ONE operand
    if insn.mnemonic in ("b", "bl") and insn.params and insn.params[0][1] == "im":
        seg, target = insn.params[0][0], insn.params[1][0]
        addr = (seg << 16) | target
        name = label_of(addr) if label_of else None
        return "%s %s" % (insn.mnemonic, name if name else "f_%05X" % addr)

    # bc: the offset operand is resolved to the label living at the target
    if insn.mnemonic == "bc":
        target = (insn.addr + insn.length + insn.params[1][0] * 2) & 0xFFFF
        name = label_of(target) if label_of else None
        return "%s %s, %s" % (insn.mnemonic, COND[insn.params[0][0]],
                              name if name else "%d" % insn.params[1][0])

    is_data_addr = insn.mnemonic in DATA_ADDR_MNEMONICS
    bodies = []
    for ix, (value, fmt) in enumerate(insn.params, start=1):
        fold = insn.offsetable and insn.offsetable == ix - 1 and fmt in ("im", "dlab")
        if fold:
            if fmt == "im" and value >= 0xFF00:
                value -= 0x10000
            text = ""
            if is_data_addr and 0x100 <= value < 0xFF00:
                name = datalabel_of(value) if datalabel_of else None
                text = name if name else "d_%05X" % value
            else:
                text = "%d" % value
            bodies[-1] = bodies[-1].replace("[", text + "[", 1)
            continue
        text = ""
        if insn.offsetable == ix:
            text += "["
        if fmt == "im" and is_data_addr and 0x100 <= value < 0xFF00:
            name = datalabel_of(value) if datalabel_of else None
            text += name if name else "d_%05X" % value
        else:
            text += format_operand(value, fmt)
        if insn.offsetable == ix:
            text += "]"
        bodies.append(text)
    head = insn.mnemonic
    if bodies:
        head += " " + ", ".join(bodies)
    return head


def resolve_params(insn, datalabel_of=None, label_of=None):
    """Turn address-like operands into (address, kind) so analysis can use them.

    Returns (mnemonic, params) where address params become ('addr', value).
    """
    out = []
    for (value, fmt) in insn.params:
        out.append((value, fmt))
    return insn.mnemonic, out


def is_branch(insn):
    if insn.mnemonic == "bc":
        return "bc"
    if insn.mnemonic in ("b", "bl"):
        return insn.mnemonic
    if insn.mnemonic == "rt" or insn.mnemonic == "rti":
        return insn.mnemonic
    if insn.mnemonic == "pop" and any(f == "pc" for (_, f) in insn.params):
        return "pop"
    return None


def branch_target(insn):
    """Absolute target of a taken branch, or None."""
    if insn.mnemonic == "bc":
        return (insn.addr + insn.length + insn.params[1][0] * 2) & 0xFFFF
    if insn.mnemonic in ("b", "bl"):
        if insn.params[0][1] == "im":
            return (insn.params[0][0] << 16) | insn.params[1][0]
        return None
    return None


def data_ref(insn):
    """(address, is_store) for memory operands that are absolute addresses."""
    if insn.mnemonic not in DATA_ADDR_MNEMONICS:
        return None
    for ix, (value, fmt) in enumerate(insn.params, start=1):
        if fmt == "im" and 0x100 <= value < 0xFF00:
            base = 0 if not insn.dsr else insn.dsr[0]
            return (base << 16) | value
    return None
