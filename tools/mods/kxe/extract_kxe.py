#!/usr/bin/env python3
"""Pull the Kuribo modules (Kuribo!/Mods/*.kxe) out of a GameCube disc image.

    python3 tools/mods/kxe/extract_kxe.py DISC OUTDIR [PREFIX]

PREFIX is the disc folder to take (default Kuribo!/Mods/). Reads the disc's file table only.
"""
import os
import struct
import sys


def files(iso):
    iso.seek(0x424)
    offset, size = struct.unpack(">II", iso.read(8))
    iso.seek(offset)
    fst = iso.read(size)
    count = struct.unpack(">I", fst[8:12])[0]
    names = count * 12

    def name(o):
        return fst[names + o:fst.index(b"\0", names + o)].decode("latin1")

    def walk(i, end, path):
        while i < end:
            kind = fst[i * 12]
            o = struct.unpack(">I", b"\0" + fst[i * 12 + 1:i * 12 + 4])[0]
            a, b = struct.unpack(">II", fst[i * 12 + 4:i * 12 + 12])
            if kind:
                yield from walk(i + 1, b, path + name(o) + "/")
                i = b
            else:
                yield path + name(o), a, b
                i += 1

    yield from walk(1, count, "")


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    disc, out = sys.argv[1], sys.argv[2]
    prefix = sys.argv[3] if len(sys.argv) == 4 else "Kuribo!/Mods/"
    os.makedirs(out, exist_ok=True)
    with open(disc, "rb") as iso:
        for path, offset, size in files(iso):
            if path.startswith(prefix):
                iso.seek(offset)
                with open(os.path.join(out, os.path.basename(path)), "wb") as f:
                    f.write(iso.read(size))
                print(path, size)


if __name__ == "__main__":
    main()
