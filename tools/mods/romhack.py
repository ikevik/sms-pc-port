#!/usr/bin/env python3
"""Apply a ROM hack's xdelta patch to your own Super Mario Sunshine image.

    python3 tools/mods/romhack.py list
    python3 tools/mods/romhack.py install sunburn --patch PATH [--iso PATH]
    python3 tools/mods/romhack.py install --patch PATH --iso PATH --out FILE    (a hack not in romhacks.json)

PATH is the hack's .xdelta file, or the .zip/.7z/.rar it came in (the patch is
found inside). The image is yours: the North American disc (GMSE01), found as
--iso, SMS_DISC_IMAGE, settings.txt or rom/ as for get.py. For a hack in
tools/mods/romhacks.json the source and the result are checked against the MD5s
recorded there; for any other the patch's own checksums are the check.
The patched image goes to mods/<id>/<name>.iso.

Needs Python 3 (7-Zip, 7z/7zz/7za on PATH, only for .7z and .rar).
"""

import argparse
import json
import os
import shutil
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import get  # noqa: E402
import vcdiff  # noqa: E402

PATCH_SUFFIXES = (".xdelta", ".xdelta3", ".vcdiff")


def registry():
    with open(os.path.join(HERE, "romhacks.json"), encoding="utf-8") as f:
        return {h["id"]: h for h in json.load(f)["romhacks"]}


def find_patch(path, want, tmp):
    """The .xdelta inside `path` (or `path` itself), unpacked under tmp."""
    if path.lower().endswith(PATCH_SUFFIXES):
        return path
    names = []
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if n.lower().endswith(PATCH_SUFFIXES)]
            names = sorted(names, key=lambda n: (os.path.basename(n) != want, n))
            if not names:
                raise get.Failure("no .xdelta file in %s" % path)
            if any(".." in n.split("/") or os.path.isabs(n) for n in names[:1]):
                raise get.Failure("the archive holds an unsafe path: %s" % names[0])
            get.remove(tmp)
            os.makedirs(tmp)
            get.say("Unpacking %s" % os.path.basename(path))
            z.extract(names[0], tmp)
        return os.path.join(tmp, names[0])
    # 7z / rar: list first, then unpack only the patch
    import subprocess
    r = subprocess.run([get.seven_zip(), "l", "-slt", path], capture_output=True, text=True)
    if r.returncode != 0:
        raise get.Failure("7-Zip could not read %s:\n%s" % (path, r.stderr.strip()))
    names = [l[7:] for l in r.stdout.splitlines() if l.startswith("Path = ")][1:]
    names = [n for n in names if n.lower().endswith(PATCH_SUFFIXES)]
    names = sorted(names, key=lambda n: (os.path.basename(n) != want, n))
    if not names:
        raise get.Failure("no .xdelta file in %s" % path)
    if any(".." in n.replace("\\", "/").split("/") for n in names[:1]):
        raise get.Failure("the archive holds an unsafe path: %s" % names[0])
    get.extract(path, tmp, [names[0]])
    return os.path.join(tmp, names[0])


def progress_printer():
    last = [0.0]

    def progress(done, total):
        now = time.time()
        if sys.stdout.isatty() and now - last[0] > 1.0:
            last[0] = now
            print("\r  %d%%   " % (100 * done // total), end="", flush=True)
        elif not sys.stdout.isatty() and now - last[0] > 5.0:
            last[0] = now
            get.say("Patching your image: %d%%" % (100 * done // total))
    return progress


def install(args):
    hack = None
    if args.id:
        hack = registry().get(args.id)
        if not hack:
            raise get.Failure("unknown ROM hack %r (python3 tools/mods/romhack.py list)" % args.id)
    elif not args.out:
        raise get.Failure("name a ROM hack from the list, or give --out FILE for another")
    iso = get.find_iso(args.iso)
    if not os.path.isfile(iso):
        raise get.Failure("no such image: %s" % iso)
    if not os.path.isfile(args.patch):
        raise get.Failure("no such file: %s" % args.patch)
    if hack:
        get.say("== %s" % hack["name"])
        get.say("Checking your image, %s" % iso)
        got = get.md5_of(iso)
        if got != hack["source_md5"]:
            raise get.Failure("%s patches only the North American disc with MD5 %s, and this one is %s"
                              " (compressed or trimmed images such as CISO, NKit and RVZ do not work)"
                              % (hack["name"], hack["source_md5"], got))
        out = args.out or os.path.join(get.MODS, hack["id"], hack["iso"])
    else:
        out = args.out
    get.need_space(os.path.dirname(os.path.abspath(out)), os.path.getsize(iso) + (64 << 20))
    tmp = os.path.join(get.DOWNLOADS, "romhack")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    try:
        patch = find_patch(args.patch, hack["patch"] if hack else "", tmp)
        get.say("Patching your image (about a minute)")
        try:
            vcdiff.apply(iso, patch, out + ".part", progress_printer())
        except vcdiff.VcdiffError as e:
            raise get.Failure("patching failed: %s" % e)
        if sys.stdout.isatty():
            print("\r        \r", end="")
        if hack and hack.get("result_md5"):
            got = get.md5_of(out + ".part")
            if got != hack["result_md5"]:
                raise get.Failure("the patched image is not the expected one (MD5 %s, expected %s)"
                                  % (got, hack["result_md5"]))
        os.replace(out + ".part", out)
    except BaseException:
        if os.path.exists(out + ".part"):
            os.remove(out + ".part")
        raise
    finally:
        get.remove(tmp)
    get.say("Wrote %s" % out)
    if hack and hack.get("notes"):
        get.say("Note: %s" % hack["notes"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("install")
    p.add_argument("id", nargs="?")
    p.add_argument("--patch", required=True)
    p.add_argument("--iso")
    p.add_argument("--out")
    args = ap.parse_args()
    try:
        if args.cmd == "list":
            for h in registry().values():
                print("%-10s %s (%s)" % (h["id"], h["name"], h["game_id"]))
        else:
            install(args)
    except get.Failure as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
