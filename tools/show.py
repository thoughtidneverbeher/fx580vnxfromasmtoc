"""Annotated function listing.

Usage: python3 tools/show.py <label|addr> [count] [-a]
       -a   show all instructions until the next function label (up to 400)

Branch targets are resolved from build/disas.json and printed as @XXXXX.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASM = os.path.join(ROOT, "resource", "disas.asm")
JSON = os.path.join(ROOT, "build", "disas.json")
LABELS = os.path.join(ROOT, "resource", "labels")

_ADDR_RE = re.compile(r";\s*([0-9A-F]{4,5})\s*\|")


def label_map():
    out = {}
    with open(LABELS) as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 2 and parts[0] != "?????":
                try:
                    out[parts[1]] = int(parts[0], 16)
                except ValueError:
                    pass
    return out


def load_db():
    with open(JSON) as f:
        return json.load(f)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    all_flag = "-a" in sys.argv
    labels = label_map()
    db = load_db()
    by_addr = {int(i["addr"], 16): i for i in db["insns"]}
    # function labels only (names, not .l_xxx locals)
    func_addrs = sorted(a for n, a in labels.items() if not n.startswith("."))

    key = args[0]
    if key in labels:
        addr = labels[key]
    else:
        addr = int(key, 0)
    count = int(args[1]) if len(args) > 1 else 40

    lines = open(ASM).read().splitlines()
    text = {}
    for line in lines:
        m = _ADDR_RE.search(line)
        if m:
            text[int(m.group(1), 16)] = line.split(";")[0].strip()

    printed = 0
    cur = addr
    while printed < count:
        if cur not in by_addr:
            cur += 2
            if cur - addr > 0x400:
                break
            continue
        ins = by_addr[cur]
        label = labels.get("%05X" % cur)
        s = "%05X  %-28s" % (cur, text.get(cur, ins["mnemonic"]))
        for p in ins["params"]:
            if isinstance(p, list) and len(p) == 2 and p[1] == "lab":
                s += "  @%05X" % p[0]
        if label and (printed == 0 or not label.startswith(".")):
            s += "   ; " + label
        print(s)
        cur += ins["length"]
        printed += 1


if __name__ == "__main__":
    main()
