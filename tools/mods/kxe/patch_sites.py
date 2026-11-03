#!/usr/bin/env python3
"""List the retail-game patches a Kuribo module installs, from its machine code.

    python3 tools/mods/kxe/patch_sites.py OUT.dol [decomp/config/GMSE01/symbols.txt]

OUT.dol is a module made by kxe2dol.py. A module patches the game when it loads, with code like

    lis r4, 0x8026; ... stw r3, -0x1f98(r4)

where r3 holds a branch to one of its functions (a call) or a plain instruction word. This runs
that code in a small model (lis, addi, addis, ori, oris, li, mr, rlwinm and stores; calls and
branches are ignored, so each function is taken as one straight line), and prints every store to a
game address: the address, the game function it is in, and either the branch it writes (to which
module offset) or the word.
"""
import bisect
import re
import struct
import sys

BASE = 0x80500000
M = 0xFFFFFFFF


def sx(v, n):
    return v - (1 << n) if v >> (n - 1) & 1 else v


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    dol = open(sys.argv[1], "rb").read()
    size = struct.unpack(">I", dol[0x90:0x94])[0]
    img = dol[0x100:0x100 + size]
    syms = []
    if len(sys.argv) == 3:
        for line in open(sys.argv[2], encoding="utf-8"):
            m = re.match(r"(\S+) = \.\w+:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)", line)
            if m:
                syms.append((int(m[2], 16), int(m[3], 16), m[1]))
    syms.sort()
    keys = [s[0] for s in syms]

    def where(a):
        i = bisect.bisect_right(keys, a) - 1
        if i >= 0 and a < syms[i][0] + syms[i][1]:
            return "%s +%#x" % (syms[i][2], a - syms[i][0])
        return "?"

    regs = {}
    out = []
    for o in range(0, len(img) & ~3, 4):
        w = struct.unpack(">I", img[o:o + 4])[0]
        op, rt, ra, u = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        d = sx(u, 16)
        if w == 0x7C0802A6:                       # mflr: a new function
            regs = {}
        elif op == 15:                            # lis / addis
            b = 0 if ra == 0 else regs.get(ra)
            regs[rt] = None if b is None else (b + (u << 16)) & M
        elif op == 14:                            # li / addi
            b = 0 if ra == 0 else regs.get(ra)
            regs[rt] = None if b is None else (b + d) & M
        elif op == 24:                            # ori
            regs[ra] = None if regs.get(rt) is None else regs[rt] | u
        elif op == 25:                            # oris
            regs[ra] = None if regs.get(rt) is None else regs[rt] | (u << 16)
        elif op == 21:                            # rlwinm
            v = regs.get(rt)
            if v is None:
                regs[ra] = None
            else:
                sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
                v = ((v << sh) | (v >> (32 - sh))) & M if sh else v
                mask = (M >> mb) & (M << (31 - me) & M) if mb <= me else ~((M >> (me + 1)) & (M << (32 - mb) & M)) & M
                regs[ra] = v & mask
        elif op == 31 and (w >> 1) & 0x3FF == 444:  # or / mr
            regs[ra] = regs.get(rt)
        elif op in (36, 37):                      # stw / stwu
            base = 0 if ra == 0 else regs.get(ra)
            value = regs.get(rt)
            if base is not None and value is not None:
                a = (base + d) & M
                if 0x80003000 <= a < 0x80420000:
                    out.append((o, a, value))
    for o, a, value in out:
        if value >> 26 == 18:
            target = (a + sx(value & 0x03FFFFFC, 26)) & M
            what = ("bl" if value & 1 else "b ") + (" module+%#x" % (target - BASE) if BASE <= target < BASE + len(img)
                                                     else " %#010x" % target)
        else:
            what = "word %#010x" % value
        print("%#07x  %#010x  %-62s %s" % (o, a, where(a), what))


if __name__ == "__main__":
    main()
