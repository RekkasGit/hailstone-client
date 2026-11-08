#!/usr/bin/env python3
"""Offline check of the name plates' depth test (docs/graphics.md "Name plates"), with the zone viewer.

Needs a GPU, a display and the game folder, so it is not part of hailstone_tests. It runs the viewer twice:

  1. --plate-test: test name plates (magenta text) over the zone, drawn as the client draws them.
  2. --plate-test-no-depth: the same plates with no depth test, the reference.

Each plate was placed from the CPU's rays into the zone so that it is known what the scene should do to it: "front"
plates lie in front of everything they cover (all of their magenta must show), "behind" plates lie behind (none may),
"partial" plates are cut (reported only). Their boxes come from the viewer's log; the magenta pixels in each box are
counted in both screenshots. Exit status 0 when every assertion holds.

  tools/probe_test.py --hailstone build/hailstone/hailstone --game-dir ~/Games/EQ1Main
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib


def read_png(path):
    """(width, height, channels, rows) of an 8-bit, non-interlaced PNG, each row a bytes of width * channels."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB(A), not interlaced")
            channels = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), width * channels, [], bytes(width * channels)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = bytes(line)
        rows.append(prev)
    return width, height, channels, rows


def magenta(png, box):
    """Pixels of the plates' colour (255, 0, 255) inside box = (x0, y0, x1, y1)."""
    width, height, ch, rows = png
    x0, y0, x1, y1 = max(box[0], 0), max(box[1], 0), min(box[2], width), min(box[3], height)
    n = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0, x1):
            r, g, b = row[x * ch], row[x * ch + 1], row[x * ch + 2]
            if r > 200 and g < 70 and b > 200:
                n += 1
    return n


def run(args, extra, shot):
    cmd = [args.hailstone, "--zone", args.zone, "--game-dir", args.game_dir, "--lighting", "modern", "--pos", *args.pos,
           "--look", *args.look, "--window-size", "960x540", "--no-audio", "--screenshot", shot] + extra
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hailstone", default="build/hailstone/hailstone")
    ap.add_argument("--game-dir", default=os.environ.get("EQ_GAME_DIR", ""))
    ap.add_argument("--zone", default="bazaar")
    ap.add_argument("--pos", nargs=3, default=["-440.2", "84.4", "13.9"])
    ap.add_argument("--look", nargs=2, default=["180", "0"])
    ap.add_argument("--keep", help="keep the screenshots in this folder")
    args = ap.parse_args()
    if not args.game_dir:
        sys.exit("need --game-dir (or EQ_GAME_DIR)")
    out = args.keep or tempfile.mkdtemp(prefix="probe_test_")
    os.makedirs(out, exist_ok=True)
    failed = []

    # the plates, with and without the depth test
    timing = ["--frames", "8", "--frame-dt", "0.05"]
    code_d, log_d = run(args, timing + ["--plate-test"], os.path.join(out, "plates.png"))
    code_r, log_r = run(args, timing + ["--plate-test-no-depth"], os.path.join(out, "plates_reference.png"))
    plates = [(int(m[1]), m[2], tuple(int(m[i]) for i in range(3, 7)))
              for m in re.finditer(r"test plate (\d+) (\w+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)", log_d)]
    if code_d != 0 or code_r != 0 or not plates:
        print(log_d[-2000:])
        sys.exit("the plate runs did not work (or the view has no plates): " + ("no plates" if not plates else f"exit {code_d}/{code_r}"))
    shown = read_png(os.path.join(out, "plates.png"))
    ref = read_png(os.path.join(out, "plates_reference.png"))
    print(f"{'plate':>5} {'kind':<8} {'reference':>9} {'with depth':>10} {'share':>6}")
    for pid, kind, box in plates:
        r, s = magenta(ref, box), magenta(shown, box)
        share = s / r if r else 0.0
        print(f"{pid:>5} {kind:<8} {r:>9} {s:>10} {share:>6.0%}")
        if r < 50:
            failed.append(f"plate {pid}: the reference shows almost nothing ({r} pixels)")
        elif kind == "front" and share < 0.95:
            failed.append(f"plate {pid} (front): only {share:.0%} of it shows")
        elif kind == "behind" and share > 0.02:
            failed.append(f"plate {pid} (behind): {share:.0%} of it shows")
        elif kind == "partial" and share > 0.98:
            failed.append(f"plate {pid} (partial): nothing was cut")
    kinds = {k for _, k, _ in plates}
    if not {"front", "behind"} <= kinds:
        failed.append("the view gave no front or no behind plate: pick another --pos / --look")

    print(f"screenshots in {out}")
    if failed:
        print("FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("passed")


if __name__ == "__main__":
    main()
