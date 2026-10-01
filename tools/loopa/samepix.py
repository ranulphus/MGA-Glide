#!/usr/bin/env python3
"""Pixel-exact comparison of two directories of frames (stdlib only).

  samepix.py BASE GOT [--glob '*.png'] [--require N]

Every image under BASE (PNG and binary PPM, recursively; .diff.png files are
skipped) must exist under GOT at the same relative path with the same size
and identical RGB pixels. Image files only in GOT are listed but do not
fail. Exit status 0 when everything matches and at least N images (default
1) were compared, else 1. For checking that a change leaves every picture
unchanged (the triangle-path performance work), unlike tools/imgcmp.py,
which allows a tolerance.
"""
import argparse
import fnmatch
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png  # noqa: E402


def load(path):
    if path.lower().endswith(".ppm"):
        return png.read_ppm(path)
    return png.read_png(path)


def images(root, pattern):
    found = []
    for d, _, files in os.walk(root):
        for f in files:
            low = f.lower()
            if low.endswith(".diff.png") or not (low.endswith(".png") or low.endswith(".ppm")):
                continue
            if pattern and not fnmatch.fnmatch(f, pattern):
                continue
            found.append(os.path.relpath(os.path.join(d, f), root))
    return sorted(found)


def first_diff(w, a, b):
    for i in range(min(len(a), len(b)) // 3):
        if a[3 * i:3 * i + 3] != b[3 * i:3 * i + 3]:
            return i % w, i // w
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("base")
    ap.add_argument("got")
    ap.add_argument("--glob", default="", help="only file names matching this pattern")
    ap.add_argument("--require", type=int, default=1, help="fail if fewer images were compared")
    a = ap.parse_args()
    base, got = images(a.base, a.glob), set(images(a.got, a.glob))
    bad = compared = 0
    for rel in base:
        g = os.path.join(a.got, rel)
        if rel not in got:
            print("MISSING  %s" % rel)
            bad += 1
            continue
        w1, h1, p1 = load(os.path.join(a.base, rel))
        w2, h2, p2 = load(g)
        compared += 1
        if (w1, h1) != (w2, h2):
            print("SIZE     %s: %dx%d vs %dx%d" % (rel, w1, h1, w2, h2))
            bad += 1
        elif p1 != p2:
            n = sum(1 for i in range(0, len(p1), 3) if p1[i:i + 3] != p2[i:i + 3])
            print("DIFFER   %s: %d pixels, first at %s" % (rel, n, first_diff(w1, p1, p2)))
            bad += 1
    for rel in sorted(got - set(base)):
        print("EXTRA    %s (not in base)" % rel)
    ok = bad == 0 and compared >= a.require
    print("samepix: %d compared, %d differ or missing%s" % (compared, bad, "" if ok else " -- FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
