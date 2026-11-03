#!/usr/bin/env python3
"""Make a GameCube .dol out of a Kuribo module (.kxe), so a disassembler can open it.

    python3 tools/mods/kxe/kxe2dol.py MODULE.kxe OUT.dol [decomp/config/GMSE01/symbols.txt]

A .kxe holds the module's code and data (one block), a table of relocations and the names of the
functions it imports from other modules. This lays the block out at 0x80500000, applies the
relocations as the Kuribo loader does, points every import at a one-instruction stub in a second
section (0x80700000, 8 bytes each, in import order) and writes OUT.dol (+ OUT.dol.names.json: the
address of each game function and import the module calls, named from the symbol list).

Header (big endian words): 0x10 text offset, 0x14 text size, 0x40/0x44 relocation table offset and
size, 0x50/0x54 import table offset and size. A relocation is 16 bytes, (kind<<24 | a<<16 | b<<8,
offset in the block, target, addend):
  0x0A  a branch (bl) at `offset`: b=0xFD game address `target`; b=0xFF import number `target`;
        otherwise the module's own `target`
  0x06  the high half, 0x04 the low half of a 16-bit address at `offset` (lis/addi pairs; the
        offset names the immediate itself); the address is base + target + addend
  0x01  a 32-bit address at `offset`
An import is 12 bytes: name offset, name length, hash.
"""
import json
import re
import struct
import sys

BASE = 0x80500000
STUB = 0x80700000


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    if data[:4] != b"KXER":
        sys.exit("not a Kuribo module (no KXER magic)")
    text_off, text_size = struct.unpack(">II", data[0x10:0x18])
    rel_off, rel_size = struct.unpack(">II", data[0x40:0x48])
    imp_off, imp_size = struct.unpack(">II", data[0x50:0x58])
    img = bytearray(data[text_off:text_off + text_size])

    first_name = struct.unpack(">I", data[imp_off:imp_off + 4])[0]
    count = (first_name - imp_off) // 12
    imports = []
    for i in range(count):
        name_off, length, _hash = struct.unpack(">III", data[imp_off + 12 * i:imp_off + 12 * i + 12])
        imports.append(data[name_off:name_off + length].split(b"\0")[0].decode())

    game = {}
    if len(sys.argv) == 4:
        for line in open(sys.argv[3], encoding="utf-8"):
            m = re.match(r"(\S+) = \.\w+:0x([0-9A-Fa-f]+);", line)
            if m:
                game.setdefault(int(m[2], 16), m[1])

    def put32(o, v):
        img[o:o + 4] = struct.pack(">I", v & 0xFFFFFFFF)

    def branch(o, target):
        word = struct.unpack(">I", img[o:o + 4])[0]
        put32(o, (word & 0xFC000003) | ((target - (BASE + o)) & 0x03FFFFFC))

    names = {}
    for i in range(rel_size // 16):
        w0, off, target, addend = struct.unpack(">4I", data[rel_off + 16 * i:rel_off + 16 * i + 16])
        kind, b = w0 >> 24, (w0 >> 8) & 255
        if kind == 0x0A:
            if b == 0xFD:
                branch(off, target)
                names[target] = game.get(target, "game_%08X" % target)
            elif b == 0xFF:
                address = STUB + 8 * target
                branch(off, address)
                names[address] = "import_" + imports[target]
            else:
                branch(off, BASE + target)
        elif kind == 0x06:
            value = BASE + target + addend
            img[off:off + 2] = struct.pack(">H", ((value >> 16) + ((value >> 15) & 1)) & 0xFFFF)
        elif kind == 0x04:
            img[off:off + 2] = struct.pack(">H", (BASE + target + addend) & 0xFFFF)
        elif kind == 0x01:
            put32(off, BASE + target + addend)
        else:
            print("unknown relocation %#x at %#x" % (w0, off), file=sys.stderr)

    stub = b"".join(struct.pack(">II", 0x4E800020, 0) for _ in imports)
    header = bytearray(0x100)
    body, position = b"", 0x100
    for index, (blob, address) in enumerate(((bytes(img), BASE), (stub, STUB))):
        struct.pack_into(">I", header, 4 * index, position + len(body))
        struct.pack_into(">I", header, 0x48 + 4 * index, address)
        struct.pack_into(">I", header, 0x90 + 4 * index, len(blob))
        body += blob + b"\0" * (-len(blob) % 32)
    struct.pack_into(">I", header, 0xE0, BASE)
    with open(out, "wb") as f:
        f.write(bytes(header) + body)
    with open(out + ".names.json", "w") as f:
        json.dump({hex(k): v for k, v in sorted(names.items())}, f, indent=1)
    print("%s: %d relocations, %d imports, %d named targets" % (src, rel_size // 16, len(imports), len(names)))


if __name__ == "__main__":
    main()
