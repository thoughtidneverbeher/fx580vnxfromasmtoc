"""
Independent re-implementation of machine/disassembler/disassembler.lua.

Why: the resource listing is the output of that Lua script, which itself depends
on a working decoder. To trust the whole reverse-engineering chain we re-derive
the listing from rom.bin alone, with our own decoder (tools/nx8.py), and diff it
against the Lua output. Anything that differs is a bug in our understanding of
the CPU, discovered before we write a single line of C.

Everything downstream (function detection, call graphs, decompilation) is built
on the `Disasm` object this module produces.
"""
import re
import sys
import os
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nx8

DATA_LABEL_FORMAT = "d_%05X"
GLOBAL_LABEL_FORMAT = "f_%05X"
LOCAL_LABEL_FORMAT = ".l_%03X"

# default reset/interrupt vectors the fx580vnx ROM actually installs
DEFAULT_ENTRIES = [1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]


class Label:
    __slots__ = ("address", "name", "xrefs", "streak", "context", "context_head", "ref_instr")

    def __init__(self, address, name, streak):
        self.address = address
        self.name = name
        self.xrefs = {}
        self.streak = streak
        self.context = None
        self.context_head = False
        self.ref_instr = None   # set when this label sits on an instruction


class Streak:
    __slots__ = ("friends", "context", "seen")

    def __init__(self):
        self.friends = {}
        self.context = None
        self.seen = False


class Insn:
    __slots__ = ("address", "length", "mnemonic", "params", "offsetable", "opcode",
                 "imm16", "dsr", "context", "under_label", "break_streak", "label")

    def __init__(self, address, length, mnemonic, params, offsetable, opcode, imm16):
        self.address = address
        self.length = length
        self.mnemonic = mnemonic
        self.params = params
        self.offsetable = offsetable
        self.opcode = opcode
        self.imm16 = imm16
        self.dsr = None
        self.context = None
        self.under_label = None
        self.break_streak = False
        self.label = None


class Disasm:
    """Disassembly of one ROM image: instructions, labels, contexts, rendering."""

    def __init__(self, rom, entries=None, rom_window=0xD000, names_path=None):
        self.rom = rom
        self.rom_window = rom_window
        self.entries = entries if entries is not None else DEFAULT_ENTRIES
        self.insns = {}          # address -> Insn
        self.labels = {}         # address -> Label
        self.data_labels = {}    # address -> Label
        self.comments = defaultdict(list)
        self.jump_tables = []
        self.unresolved = []
        self.renames = {}
        self.name_entries = []
        self.extra_entries = []
        if names_path:
            self.load_names(names_path)

    # -- primitive helpers ---------------------------------------------------
    def word(self, address):
        return self.rom[address] | (self.rom[address + 1] << 8)

    def add_label(self, streak, address, xref=None):
        label = self.labels.get(address)
        if label is None:
            label = Label(address, GLOBAL_LABEL_FORMAT % address, streak)
            self.labels[address] = label
        if xref is not None:
            label.xrefs[xref] = True
        return label

    def add_data_label(self, address, xref=None):
        label = self.data_labels.get(address)
        if label is None:
            label = Label(address, DATA_LABEL_FORMAT % address, None)
            self.data_labels[address] = label
        if xref is not None:
            label.xrefs[xref] = True
        return label

    def add_comment(self, address, comment):
        self.comments[address].append(comment)

    # -- decoding ------------------------------------------------------------
    def disassemble(self, address):
        """Decode one (possibly dsr-prefixed) instruction at `address`.

        A `dsr` prefix is a separate 2-byte instruction that only sets the
        data-segment register for the *next* instruction; disassembler.lua folds
        it into the following instruction (both words and the printed prefix).
        """
        if address in self.insns:
            return self.insns[address], True
        cursor = address
        dsr = None
        words = []
        while True:
            decoded = nx8.decode(self.rom, cursor)
            words.extend(decoded.words)
            if decoded.mnemonic == "dsr":
                dsr = decoded.params[0]
                cursor += decoded.length
                continue
            insn = Insn(address, cursor + decoded.length - address, decoded.mnemonic,
                        decoded.params, decoded.offsetable, words, decoded.imm16)
            insn.dsr = dsr
            self.insns[address] = insn
            return insn, False

    # -- phase 1: find code --------------------------------------------------
    def sweep(self, extra_entries=()):
        """Discover code from the vector table (and optional extra addresses).

        Addresses are 24-bit. Walking a chain wraps at 64KB boundaries because
        the CPU fetches through a 16-bit PC with a 16-bit code-segment register
        (CSR); disassembler.lua models the segment as (address & 0xF0000).
        """
        to_disassemble = set()
        for index in self.entries:
            address = self.word(2 * index)
            if address % 2 == 0:
                self.add_disassemble_address(address, to_disassemble)
        for address in extra_entries:
            self.add_disassemble_address(address, to_disassemble)
        while to_disassemble:
            new_to_disassemble = set()
            variable_branches = []
            for entry in to_disassemble:
                segment, address, streak = entry
                address = segment | address
                if address >= len(self.rom):
                    print("out of data at %05X" % address, file=sys.stderr)
                    continue
                while True:
                    insn, seen = self.disassemble(address)
                    if seen:
                        break
                    if insn.mnemonic == "?":
                        print("unknown instruction at %05X" % address, file=sys.stderr)
                        break
                    segment = address & 0xF0000
                    address = segment | ((address + insn.length) & 0xFFFF)
                    if insn.mnemonic in ("b", "bl") and insn.params and insn.params[0][1] != "im":
                        variable_branches.append((insn, streak))
                    pop_names = nx8.expand_pushpop(insn) if insn.mnemonic == "pop" else None
                    if insn.mnemonic in ("rt", "rti") or (pop_names is not None and "pc" in pop_names):
                        insn.break_streak = True
                    if insn.mnemonic == "bc":
                        target = segment | ((insn.address + insn.length + insn.params[1][0] * 2) & 0xFFFF)
                        label = self.add_label(streak, target, insn)
                        new_to_disassemble.add((target & 0xF0000, target & 0xFFFF, streak))
                        insn.params[1] = (label.address, "lab")
                        if insn.params[0][0] == 0xE:
                            insn.break_streak = True
                    if insn.mnemonic in ("b", "bl"):
                        if insn.params[0][1] == "im":
                            target = (insn.params[0][0] << 16) | insn.params[1][0]
                            new_streak = Streak()
                            label = self.add_label(new_streak, target, insn)
                            new_to_disassemble.add((target & 0xF0000, target & 0xFFFF, new_streak))
                            insn.params = [(label.address, "lab")]
                        if insn.mnemonic == "b":
                            insn.break_streak = True
                    if insn.mnemonic in nx8.DATA_ADDR_MNEMONICS and (not insn.dsr):
                        for ix, (value, fmt) in enumerate(insn.params):
                            if fmt == "im" and 0x100 <= value < 0xFF00:
                                label = self.add_data_label(value, insn)
                                insn.params[ix] = (label.address, "dlab")
                    if insn.break_streak:
                        break
            to_disassemble = new_to_disassemble
            # deferred: register-indexed jump tables are resolved after the pass,
            # exactly like disassembler.lua, so their targets feed the next pass
            for (insn, streak) in variable_branches:
                if not self.resolve_variable_branch(insn, to_disassemble, streak):
                    self.unresolved.append(insn.address)
                    self.add_comment(insn.address, "Failed to resolve variable branch")

    def add_disassemble_address(self, address, to_disassemble, streak=None):
        if streak is None:
            streak = Streak()
        label = self.add_label(streak, address)
        to_disassemble.add((address & 0xF0000, address & 0xFFFF, label.streak))

    def ascii_comment(self, addr):
        """'; Equiv: <op> erN, value | 0xkey | "-value" | "ascii"' annotations.

        disassembler.lua adds these in a pass over *all* instructions after the
        context discovery, comparing each instruction with its predecessor.
        """
        insn = self.insns.get(addr)
        if insn is None:
            return
        previous = self.insns.get(insn.address - 2)
        if previous is None or previous.address + previous.length != insn.address:
            return
        next_opcode = {"mov": "mov", "cmp": "cmpc", "add": "addc", "sub": "subc"}
        if next_opcode.get(previous.mnemonic) != insn.mnemonic:
            return
        if previous.context is not insn.context:
            return
        if previous.params[0][1] != "r" or insn.params[0][1] != "r":
            return
        if previous.params[1][1] != "im" or insn.params[1][1] != "im":
            return
        r = previous.params[0][0]
        if r % 2 or insn.params[0][0] != r + 1:
            return
        val = (insn.params[1][0] * 256 + previous.params[1][0]) & 0xFFFF
        repr_ = "%d | %04X" % (val, val)
        if val >= 0x8000:
            repr_ += " | -%d" % (0x10000 - val)
        if val + 16 < len(self.rom):
            text = b""
            length = 0
            while val + length < len(self.rom):
                char = self.rom[val + length]
                if char == 0:
                    break
                if not 0x20 <= char <= 0x7E:
                    text = None
                    break
                text += bytes([char])
                length += 1
                if length > 16:
                    break
            if text and 0 < length <= 16:
                repr_ += ' | "%s"' % text.decode("ascii").replace('"', '\\"')
        self.add_comment(previous.address, "Equiv: %s er%d, %s" % (previous.mnemonic, r, repr_))

    def previous_instr(self, insn):
        """Instruction immediately preceding `insn` (skipping immediate words)."""
        address = insn.address - 2
        while address >= 0:
            candidate = self.insns.get(address)
            if candidate is not None:
                if candidate.address + candidate.length == insn.address:
                    return candidate
                return None
            address -= 2
        return None

    def resolve_variable_branch(self, bl_instr, to_disassemble, streak):
        """Port of resolve_variable_branch: register-indexed jump tables.

            cmp A, B / cmpc A, B / bc <cond> / (b ...)*
            add A, B / addc A, B
            sllc A / sll A
            l A, table[A]
            b A
        The comparison chain bounds the index range, so every entry of the
        table is a reachable branch target and must be disassembled.
        """
        def match(instr, mnemonic):
            return instr is not None and instr.mnemonic == mnemonic

        l_instr = self.previous_instr(bl_instr)
        if not match(l_instr, "l") or l_instr.dsr:
            return False
        if len(l_instr.params) < 3 or l_instr.params[2][1] != "dlab":
            return False
        map_base = l_instr.params[2][0]
        sll_instr = self.previous_instr(l_instr)
        if not match(sll_instr, "sll"):
            return False
        sllc_instr = self.previous_instr(sll_instr)
        if not match(sllc_instr, "sllc"):
            return False
        offset_instr = self.previous_instr(sllc_instr)

        offset = 0
        offset_instr_2 = self.previous_instr(offset_instr)
        branch_4 = None
        if match(offset_instr, "addc"):
            offset += offset_instr.params[1][0] * 256
        else:
            offset_instr_2 = offset_instr
        branch_4 = self.previous_instr(offset_instr_2)
        if match(offset_instr_2, "add"):
            offset += offset_instr_2.params[1][0]
        else:
            branch_4 = offset_instr_2

        cmp_1 = cmpc_1 = branch_1 = branch_3 = cmpc_2 = cmp_2 = None
        # NOTE: disassembler.lua uses `repeat ... until true`, i.e. ONE pass.
        branch_3 = self.previous_instr(branch_4)
        if not match(branch_4, "b"):
            branch_3 = branch_4
        if not match(branch_3, "bc"):
            return False
        cmpc_2 = self.previous_instr(branch_3)
        if not match(cmpc_2, "cmpc"):
            return False
        cmp_2 = self.previous_instr(cmpc_2)
        if not match(cmp_2, "cmp"):
            return False
        branch_2 = self.previous_instr(cmp_2)
        branch_1 = self.previous_instr(branch_2)
        if not match(branch_2, "b"):
            branch_1 = branch_2
        if not match(branch_1, "bc"):
            cmpc_1 = None
        elif branch_1.params[0][0] > 3:
            cmpc_1 = None
        else:
            cmpc_1 = self.previous_instr(branch_1)
            if not match(cmpc_1, "cmpc"):
                return False
            cmp_1 = self.previous_instr(cmpc_1)
            if not match(cmp_1, "cmp"):
                return False

        cmp_value, cmp_cond = {}, {}
        if cmp_1 is not None:
            cmp_value[1] = cmp_1.params[1][0] + (cmpc_1.params[1][0] * 256)
            cmp_cond[1] = branch_1.params[0][0]
        cmp_value[2] = cmp_2.params[1][0] + (cmpc_2.params[1][0] * 256)
        cmp_cond[2] = branch_3.params[0][0]

        lo = hi = None
        for ix in (1, 2):
            cond = cmp_cond.get(ix)
            if cond is None:
                continue
            if cond == 0:
                lo = cmp_value[ix]
            elif cond == 1:
                hi = cmp_value[ix] - 1
            elif cond == 2:
                lo = cmp_value[ix] + 1
            elif cond == 3:
                hi = cmp_value[ix]
        if hi is None and lo is not None:
            hi = lo - 1
            lo = 0
        if lo is None and hi is not None:
            lo = 0
        if lo is None or hi is None:
            return False
        lo += offset
        hi += offset
        if hi < lo:
            lo, hi = hi + 1, lo - 1
        lo &= 0xFFFF
        hi &= 0xFFFF

        self.add_comment(bl_instr.address, "Jump table")
        self.jump_tables.append((bl_instr.address, lo, hi, map_base))
        # the table holds 16-bit offsets, resolved in the *caller's* code segment
        segment = bl_instr.address & 0xF0000
        for ix in range(lo, hi + 1):
            target = segment | self.word((map_base + ix * 2) & 0xFFFF)
            label = self.add_label(streak, target, bl_instr)
            to_disassemble.add((target & 0xF0000, target & 0xFFFF, streak))
            self.add_comment(bl_instr.address, ("jt", ix, label))
        return True

    # -- phase 2: contexts ---------------------------------------------------
    def compute_contexts(self):
        """Group labels into 'contexts' (the Lua disassembler's streak chains).

        A context is the reachability component that ends up sharing one
        function-like scope: its first label keeps the f_XXXXX name, the rest
        become local labels .l_NNN relative to it.
        """
        # pass 1: chain consecutive labels and branch targets into streaks.
        # We must walk exactly like disassembler.lua's image_iterator: it steps
        # by an instruction's length, so a label sitting on the immediate half
        # of an instruction is never visited (and never joins a streak chain).
        seen_streaks = set()
        last_label = None
        address = 0
        while address < len(self.rom):
            label = self.labels.get(address)
            insn = self.insns.get(address)
            if insn is not None and label is not None:
                if last_label is not None and last_label.streak is not label.streak:
                    last_label.streak.friends[label.streak] = True
                    label.streak.friends[last_label.streak] = True
                last_label = label
                seen_streaks.add(label.streak)
            if insn is not None:
                if insn.mnemonic == "bc" or (insn.mnemonic == "b" and insn.params[0][1] == "lab"):
                    other = self.labels.get(insn.params[1][0] if insn.mnemonic == "bc"
                                            else insn.params[0][0])
                    if other is not None and last_label is not None and other.streak is not last_label.streak:
                        other.streak.friends[last_label.streak] = True
                        last_label.streak.friends[other.streak] = True
                if insn.break_streak:
                    last_label = None
            address += insn.length if insn is not None else 2
        # connected components of the streak graph = contexts
        for streak in seen_streaks:
            if streak.seen:
                continue
            component = set()
            pending = {streak}
            streak.seen = True
            while pending:
                nxt = set()
                for s in pending:
                    component.add(s)
                    for friend in s.friends:
                        if not friend.seen:
                            friend.seen = True
                            nxt.add(friend)
                pending = nxt
            context = {}
            for s in component:
                s.context = context
        for label in self.labels.values():
            label.context = label.streak.context

        order = sorted(self.labels.items())
        # local labels: everything after the context head becomes .l_NNN
        contexts_seen = set()
        rewrite_from, rewrite_to = None, None
        last_label = None
        for address, label in order:
            if last_label is not None and label.context is not last_label.context \
                    and id(label.context) in contexts_seen:
                if label.context is not rewrite_from:
                    rewrite_from, rewrite_to = label.context, {}
                label.context = rewrite_to
            else:
                rewrite_from = None
            if label.context is not None:
                contexts_seen.add(id(label.context))
            last_label = label
        for address, label in order:
            if label.context is None:
                continue
            if label.context.get("name"):
                label.name = LOCAL_LABEL_FORMAT % (address - label.context["head"])
            else:
                label.context["name"] = label.name
                label.context["head"] = address
                label.context_head = True
        last_label = None
        for address, insn in sorted(self.insns.items()):
            if insn.address in self.labels:
                last_label = self.labels[insn.address]
            insn.context = last_label.context if last_label else None
            insn.under_label = last_label
        # data labels that point into code are printed relative to the
        # instruction they land on ("<local>+<offset>"), like disassembler.lua.
        # An address may be the immediate half of an instruction; disassembler.lua
        # follows `.head` in that case, so we look for the covering instruction.
        insn_by_range = {}
        for insn in self.insns.values():
            for off in range(0, insn.length, 2):
                insn_by_range[insn.address + off] = insn
        for address, label in self.data_labels.items():
            if address >= 0x10000 or address < self.rom_window:
                insn = insn_by_range.get(address)
                if insn is not None:
                    label.ref_instr = insn

    # -- labels file (renames + extra code entry points) ---------------------
    def load_names(self, path):
        """resource/labels: '<address|f_XXXXX|d_XXXXX|.l_nnn>  <name>' per line.

        Address lines are also *entry points* for the sweep (the disassembler
        cannot reach every function from the vector table alone). The trailing
        name is applied to the label at that address. A `.l_nnn` entry refers to
        the local label that follows the most recent global entry.
        """
        self.name_entries = []
        self.extra_entries = []
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) < 2:
                    continue
                raw, real = parts[0], parts[1]
                if re.fullmatch(r"[0-9A-Fa-f]+", raw):
                    addr = int(raw, 16)
                    self.extra_entries.append(addr)
                    self.name_entries.append((addr, real))
                else:
                    self.name_entries.append((raw, real))
        return self.extra_entries

    def apply_renames(self):
        """Apply the labels file exactly like disassembler.lua's rename pass."""
        raw_to_real = {}
        last_global = None
        for raw, real in self.name_entries:
            if isinstance(raw, int):
                label = self.labels.get(raw)
                if label is None:
                    continue
                if label.context_head:
                    last_global = label.name
                    key = label.name
                else:
                    if label.name.startswith(".") and label.context:
                        key = "%s%s" % (label.context.get("name"), label.name)
                    else:
                        key = label.name
            elif raw.startswith("."):
                if last_global is None:
                    continue
                key = "%s%s" % (last_global, raw)
            else:
                last_global = raw
                key = raw
            raw_to_real[key] = real
        for address, label in self.labels.items():
            key = label.name
            if label.name.startswith(".") and label.context:
                key = "%s%s" % (label.context.get("name"), label.name)
            real = raw_to_real.get(key)
            if real:
                label.name = real
        for address, label in self.labels.items():
            if label.context:
                real = raw_to_real.get(label.context.get("name"))
                if real:
                    label.context["name"] = real
        for address, label in self.data_labels.items():
            real = raw_to_real.get(label.name)
            if real:
                label.name = real

    # -- phase 3: render -----------------------------------------------------
    def label_name_for(self, label, instr):
        """Name of a referenced label, qualified with its context when needed."""
        if label.name.startswith(".") and instr is not None and instr.context is not label.context \
                and label.context and not label.context_head:
            return "%s%s" % (label.context["name"], label.name)
        return label.name

    def data_label_name(self, label, instr):
        """Data label text: d_XXXXX, or '<local label>+<offset>' inside code."""
        if label.ref_instr is not None:
            under = label.ref_instr.under_label
            if under is not None:
                return "%s+%d" % (self.label_name_for(under, instr),
                                  label.address - under.address)
        return label.name

    def render(self):
        out = []
        last_instr = None
        address = 0
        while address < len(self.rom):
            insn = self.insns.get(address)
            next_address = address + (insn.length if insn else 2)
            if ((last_instr is None) != (insn is None)) or (
                    last_instr is not None and insn is not None and last_instr.context is not insn.context):
                out.append("")
                out.append("")
                out.append("")
            for comment in self.comments.get(address, []):
                if isinstance(comment, tuple):
                    _, ix, label = comment
                    out.append("; " + ("%6i | %s" % (ix, self.label_name_for(label, insn))))
                else:
                    out.append("; " + comment)
            label = self.labels.get(address)
            if label is not None:
                out.append("%s:" % label.name)
            if insn is not None:
                head = self.format_insn(insn)
                tail = "; %05X |" % address + "".join(" %04X" % w for w in insn.opcode)
                out.append("\t%-30s %s" % (head, tail))
            else:
                if address >= 0x10000 or address < self.rom_window:
                    datalabel = self.data_labels.get(address)
                    if datalabel is not None:
                        out.append("%s:" % datalabel.name)
                data = self.word(address)
                out.append("\tdw 0x%04X                      ; %05X | %04X" % (data, address, data))
            last_instr = insn
            address = next_address
        return "\n".join(out) + "\n"

    def format_insn(self, insn):
        names = nx8.expand_pushpop(insn)
        if names is not None:
            return insn.mnemonic + ((" " + ", ".join(names)) if names else "")
        if insn.mnemonic in ("b", "bl") and insn.params and insn.params[0][1] == "lab":
            return "%s %s" % (insn.mnemonic, self.label_name_for(self.labels[insn.params[0][0]], insn))
        if insn.mnemonic == "bc":
            # the sweep already resolved the target and stored it in params[1]
            if insn.params[1][1] == "lab":
                label = self.labels.get(insn.params[1][0])
                text = self.label_name_for(label, insn) if label else "%d" % insn.params[1][0]
            else:
                target = (insn.address + insn.length + insn.params[1][0] * 2) & 0xFFFF
                label = self.labels.get(target)
                text = self.label_name_for(label, insn) if label else "%d" % insn.params[1][0]
            return "%s %s, %s" % (insn.mnemonic, nx8.format_operand(insn.params[0][0], "co"), text)
        is_data_addr = insn.mnemonic in nx8.DATA_ADDR_MNEMONICS
        bodies = []
        for ix, (value, fmt) in enumerate(insn.params, start=1):
            if insn.offsetable and insn.offsetable == ix - 1 and fmt in ("im", "dlab"):
                if fmt == "im" and value >= 0xFF00:
                    value -= 0x10000
                text = ""
                if fmt == "dlab":
                    text = self.data_label_name(self.data_labels[value], insn)
                elif is_data_addr and 0x100 <= value < 0xFF00:
                    text = self.data_label_name(self.data_labels[value], insn)
                else:
                    text = "%d" % value
                bodies[-1] = bodies[-1].replace("[", text + "[", 1)
                continue
            text = ""
            if insn.offsetable == ix:
                if insn.dsr is not None:
                    text += nx8.format_operand(insn.dsr[0], insn.dsr[1]) + ":"
                text += "["
            if fmt == "lab":
                text += self.label_name_for(self.labels[value], insn)
            elif fmt == "dlab":
                text += self.data_label_name(self.data_labels[value], insn)
            elif fmt == "im" and is_data_addr and 0x100 <= value < 0xFF00:
                text += self.data_label_name(self.data_labels[value], insn)
            else:
                text += nx8.format_operand(value, fmt)
            if insn.offsetable == ix:
                text += "]"
            bodies.append(text)
        return insn.mnemonic + ((" " + ", ".join(bodies)) if bodies else "")


def disassemble(rom, entries=None, names_path=None):
    d = Disasm(rom, entries, names_path=names_path)
    d.sweep(extra_entries=d.extra_entries)
    d.compute_contexts()
    for address in sorted(d.insns):
        d.ascii_comment(address)
    if names_path:
        d.apply_renames()
    return d


if __name__ == "__main__":
    with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "resource", "rom.bin"), "rb") as f:
        rom = f.read()
    d = disassemble(rom)
    path = "/tmp/opencode/raw.txt"
    if len(sys.argv) > 1:
        path = sys.argv[1]
    with open(path, "w") as f:
        f.write(d.render())
    print("instructions: %d, labels: %d, data labels: %d" %
          (len(d.insns), len(d.labels), len(d.data_labels)))
