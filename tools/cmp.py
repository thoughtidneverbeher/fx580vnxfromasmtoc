"""Diff our disassembly against the Lua reference with context."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.environ.get("TMPDIR", "/tmp/opencode")


def main():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import disasm
    rom = open(os.path.join(ROOT, "resource", "rom.bin"), "rb").read()
    d = disasm.disassemble(rom, names_path=os.path.join(ROOT, "resource", "labels"))
    with open(os.path.join(TMP, "ours.txt"), "w") as f:
        f.write(d.render())
    ctx = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    a = [l.rstrip() for l in open(os.path.join(TMP, "lua_raw.txt")) if l.strip()]
    b = [l.rstrip() for l in open(os.path.join(TMP, "ours.txt")) if l.strip()]
    i = j = 0
    shown = 0
    n = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            i += 1
            j += 1
            continue
        n += 1
        if shown < limit:
            print("=== divergence #%d at lua line %d / ours line %d" % (n, i + 1, j + 1))
            lo = max(0, i - ctx)
            for k in range(lo, min(len(a), i + ctx)):
                print("L %5d | %s" % (k + 1, a[k]))
            print("   ---")
            for k in range(max(0, j - ctx), min(len(b), j + ctx)):
                print("O %5d | %s" % (k + 1, b[k]))
            print()
        # resync: skip forward on the side that is missing an equivalent line
        ai = i
        bj = j
        window = 60
        found = False
        for skip_a in range(0, window):
            for skip_b in range(0, window):
                if skip_a + skip_b > window:
                    break
                if i + skip_a < len(a) and j + skip_b < len(b) and a[i + skip_a] == b[j + skip_b]:
                    ai = i + skip_a
                    bj = j + skip_b
                    found = True
                    break
            if found:
                break
        if not found:
            break
        if shown < limit:
            print("   resync: lua skips %d lines, ours skips %d lines (addr lua=%s ours=%s)" %
                  (ai - i, bj - j,
                   a[ai].split("|")[-2].strip() if "|" in a[ai] else "?",
                   b[bj].split("|")[-2].strip() if "|" in b[bj] else "?"))
        i, j = ai, bj
        shown += 1
    print("total divergence points: %d; remaining: lua=%d ours=%d" % (n, len(a) - i, len(b) - j))


if __name__ == "__main__":
    main()
