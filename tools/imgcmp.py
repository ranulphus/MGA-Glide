#!/usr/bin/env python3
"""Compare two RGB images with a per-channel tolerance (stdlib only).

Both images are quantised to RGB565 first. Options:
  tol       per-channel difference (0-255) a pixel may have and still match
  frac      fraction of pixels allowed beyond tol
  edge      ignore pixels on a colour edge of the reference (1-px border)
  box       compare 4x4 box-filtered images (stipple translucency on G100)
  ignore    rectangles [x0, y0, x1, y1) excluded from the comparison: known
            differences of the reference card, documented in the manifest
Writes an optional diff image (red where beyond tol, grey reference).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "loopa"))
import png  # noqa: E402


def q565(rgb):
    out = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i] >> 3, rgb[i + 1] >> 2, rgb[i + 2] >> 3
        out[i], out[i + 1], out[i + 2] = (r * 255) // 31, (g * 255) // 63, (b * 255) // 31
    return bytes(out)


def box4(w, h, rgb):
    out = bytearray(len(rgb))
    for y in range(h):
        for x in range(w):
            acc = [0, 0, 0]
            n = 0
            for dy in range(-1, 3):
                for dx in range(-1, 3):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h:
                        i = (yy * w + xx) * 3
                        acc[0] += rgb[i]; acc[1] += rgb[i + 1]; acc[2] += rgb[i + 2]
                        n += 1
            i = (y * w + x) * 3
            out[i], out[i + 1], out[i + 2] = acc[0] // n, acc[1] // n, acc[2] // n
    return bytes(out)


def edge_mask(w, h, rgb):
    m = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            c = rgb[i:i + 3]
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h:
                    j = (yy * w + xx) * 3
                    if max(abs(rgb[j + k] - c[k]) for k in range(3)) > 24:
                        m[y * w + x] = 1
                        break
    return m


def compare(ref_path, got_path, tol=24, frac=0.005, edge=True, box=False, diff_path=None, ignore=()):
    rw, rh, ref = png.read_png(ref_path)
    gw, gh, got = png.read_png(got_path)
    if (rw, rh) != (gw, gh):
        return dict(ok=False, reason="size %dx%d vs %dx%d" % (rw, rh, gw, gh))
    ref, got = q565(ref), q565(got)
    if box:
        ref, got = box4(rw, rh, ref), box4(rw, rh, got)
    mask = edge_mask(rw, rh, ref) if edge else None
    bad = 0
    worst = 0
    exact = 0
    counted = 0
    diff = bytearray(len(ref)) if diff_path else None
    skip = None
    if ignore:
        skip = bytearray(rw * rh)
        for x0, y0, x1, y1 in ignore:
            for y in range(max(0, y0), min(rh, y1)):
                for x in range(max(0, x0), min(rw, x1)):
                    skip[y * rw + x] = 1
    for p in range(rw * rh):
        i = p * 3
        if skip is not None and skip[p]:
            if diff is not None:
                diff[i] = diff[i + 1] = diff[i + 2] = 0
            continue
        d = max(abs(ref[i] - got[i]), abs(ref[i + 1] - got[i + 1]), abs(ref[i + 2] - got[i + 2]))
        if mask is not None and mask[p]:
            if diff is not None:
                diff[i] = diff[i + 1] = diff[i + 2] = 40
            continue
        counted += 1
        if d == 0:
            exact += 1
        worst = max(worst, d)
        if d > tol:
            bad += 1
            if diff is not None:
                diff[i], diff[i + 1], diff[i + 2] = 255, 0, 0
        elif diff is not None:
            g = (ref[i] + ref[i + 1] + ref[i + 2]) // 6
            diff[i] = diff[i + 1] = diff[i + 2] = g
    if diff_path:
        png.write_png(diff_path, rw, rh, bytes(diff))
    f = bad / max(counted, 1)
    return dict(ok=f <= frac, bad=bad, frac=round(f, 6), worst=worst,
                exact=round(exact / max(counted, 1), 4), counted=counted)


if __name__ == "__main__":
    import argparse
    import json
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("got")
    ap.add_argument("--tol", type=int, default=24)
    ap.add_argument("--frac", type=float, default=0.005)
    ap.add_argument("--no-edge", action="store_true")
    ap.add_argument("--box", action="store_true")
    ap.add_argument("--diff")
    a = ap.parse_args()
    r = compare(a.ref, a.got, a.tol, a.frac, not a.no_edge, a.box, a.diff)
    print(json.dumps(r))
    sys.exit(0 if r.get("ok") else 1)
