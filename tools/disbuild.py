"""
Verification + IR build.

Step 1: run the Lua disassembler (reference implementation) on resource/rom.bin.
Step 2: run our own independent disassembler (tools/disasm.py) on the same ROM.
Step 3: diff the two listings. Any difference means our decoder or our code
        discovery differs from the reference -> the C we write later would be
        built on a misunderstanding. This must be 100% before proceeding.

Step 4: emit build/disas.json: the machine-readable IR used by analysis tools.

Usage: python3 tools/disbuild.py [-q]
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, "resource", "rom.bin")
BUILD = os.path.join(ROOT, "build")
LUA = os.path.join(ROOT, "machine", "disassembler", "disassembler.lua")
TMP = os.environ.get("TMPDIR", "/tmp/opencode")
ENTRY = "1,2,4,5,6,7,8,9,10,11,12,13,14,15,16"


def lua_disassemble():
    out = os.path.join(TMP, "lua_raw.txt")
    proc = subprocess.run(
        ["lua", LUA,
         "input=%s" % ROM, "output=%s" % out,
         "entry=%s" % ENTRY, "strict=true", "addresses=true",
         "word_commands=true", "rom_window=0xd000",
         "names=%s" % os.path.join(ROOT, "resource", "labels")],
        capture_output=True, text=True)
    if not os.path.exists(out):
        sys.stderr.write(proc.stdout + proc.stderr)
        raise RuntimeError("lua disassembler failed")
    with open(out) as f:
        return f.read().splitlines()


def main():
    quiet = "-q" in sys.argv
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import disasm
    with open(ROM, "rb") as f:
        rom = f.read()

    reference = lua_disassemble()
    d = disasm.disassemble(rom, names_path=os.path.join(ROOT, "resource", "labels"))
    mine = d.render().splitlines()
    if not quiet:
        print("lua lines: %d   ours: %d" % (len(reference), len(mine)))

    # blank lines are only cosmetic section separators in the reference
    # (they depend on context grouping); they carry no information.
    a = [l.rstrip() for l in reference if l.strip()]
    b = [l.rstrip() for l in mine if l.strip()]
    bad = 0
    shown = 0
    for ix in range(max(len(a), len(b))):
        la = a[ix] if ix < len(a) else "<missing>"
        lb = b[ix] if ix < len(b) else "<missing>"
        if la != lb:
            bad += 1
            if shown < 30:
                print("line %d\n  lua : %r\n  ours: %r" % (ix + 1, la, lb))
                shown += 1
    print("MISMATCHES: %d / %d lines" % (bad, max(len(a), len(b))))

    os.makedirs(BUILD, exist_ok=True)
    with open(os.path.join(BUILD, "disas.json"), "w") as f:
        json.dump({
            "labels": {"%05X" % a: l.name for a, l in sorted(d.labels.items())},
            "data_labels": {"%05X" % a: l.name for a, l in sorted(d.data_labels.items())},
            "insns": [{
                "addr": "%05X" % i.address,
                "length": i.length,
                "mnemonic": i.mnemonic,
                "params": [[v, fmt] for (v, fmt) in i.params],
                "offsetable": i.offsetable,
                "words": ["%04X" % w for w in i.opcode],
            } for i in sorted(d.insns.values(), key=lambda i: i.address)],
            "jump_tables": d.jump_tables,
        }, f, separators=(",", ":"))
    if not quiet:
        print("instructions: %d, labels: %d, data labels: %d, jump tables: %d" %
              (len(d.insns), len(d.labels), len(d.data_labels), len(d.jump_tables)))
        print("wrote build/disas.json")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
