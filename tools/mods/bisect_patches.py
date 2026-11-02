#!/usr/bin/env python3
"""Run a code-mod build with the patches of some source files switched off.

    python3 tools/mods/bisect_patches.py --exe build/macos-arm64-sunburn/sms --iso DISC --list
    python3 tools/mods/bisect_patches.py --exe build/macos-arm64-sunburn/sms --iso DISC \\
        --disable green_mode.cpp,sunburn.cpp --save /tmp/bisect-1

To find which patches of a mod change the game, a patch can be switched off by its retail
address (SMS_MOD_DISABLE, platform/mods/modhooks.cpp); this does it by source file. It starts
the game once for a few seconds to read its patch list (SMS_MOD_LIST), then starts it for real
with every patch from the named files off. A name is a file's base name (green_mode.cpp) or a
folder and file (patches/water.cpp). Use a new --save folder for each run: a save keeps what
was collected in it.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
import time

PATCH = re.compile(r"^\[mod\] (?:bl|b |w ) ([0-9a-f]{8}) -> [0-9a-f]+(?: \(off\))? \((.*):\d+\)$")


def patch_list(exe, iso, seconds):
    """{source file path: [addresses]} for the patches a start-up registers."""
    with tempfile.TemporaryDirectory() as save:
        env = dict(os.environ, SMS_MOD_LIST="1", SMS_AUDIO="0", SMS_SAVE_DIR=save)
        env.pop("SMS_MOD_DISABLE", None)
        log = os.path.join(save, "list.log")
        with open(log, "w") as out:
            game = subprocess.Popen([exe, iso], env=env, stdout=out, stderr=subprocess.STDOUT)
            deadline = time.time() + seconds
            while time.time() < deadline and game.poll() is None:
                time.sleep(0.5)
                with open(log) as f:
                    if re.search(r'module ".*" started', f.read()):
                        break
            game.terminate()
            try:
                game.wait(10)
            except subprocess.TimeoutExpired:
                game.kill()
        files = collections.defaultdict(list)
        with open(log) as f:
            for line in f:
                m = PATCH.match(line.rstrip())
                if m:
                    files[m.group(2)].append(m.group(1))
    return files


def matches(path, name):
    path = path.replace("\\", "/")
    return path.endswith("/" + name) or path == name


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="the game build (sms)")
    ap.add_argument("--iso", required=True, help="the disc image to play")
    ap.add_argument("--list", action="store_true", help="only list the patches per source file")
    ap.add_argument("--disable", default="", help="comma-separated source files whose patches go off")
    ap.add_argument("--save", help="the save folder (SMS_SAVE_DIR); a new one for each run")
    ap.add_argument("--wait", type=int, default=40, help="seconds to wait for the patch list")
    args = ap.parse_args()

    files = patch_list(args.exe, args.iso, args.wait)
    if not files:
        sys.exit("no patches were listed: is this a code-mod build, and does the disc start?")
    if args.list:
        for path, addresses in sorted(files.items(), key=lambda item: -len(item[1])):
            print("%4d  %s" % (len(addresses), path.split("eclipse-src/")[-1].split("platform/mods/")[-1]))
        return
    names = [n.strip() for n in args.disable.split(",") if n.strip()]
    chosen = set()
    for name in names:
        found = [p for p in files if matches(p, name)]
        if not found:
            sys.exit("no patches come from %r (--list shows the files)" % name)
        for p in found:
            chosen.update(files[p])
            print("off: %3d patches from %s" % (len(files[p]), p.split("eclipse-src/")[-1]))
    env = dict(os.environ)
    if chosen:
        env["SMS_MOD_DISABLE"] = ",".join(sorted(chosen))
    if args.save:
        os.makedirs(args.save, exist_ok=True)
        env["SMS_SAVE_DIR"] = args.save
    print("starting the game with %d patches off" % len(chosen))
    sys.stdout.flush()
    os.execve(args.exe, [args.exe, args.iso], env)


if __name__ == "__main__":
    main()
