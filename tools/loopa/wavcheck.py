#!/usr/bin/env python3
"""Check a Loop A --wav recording (OUT/audio.wav) for tones.

86Box does not run in step with the wall clock and OpenAL Soft's wave backend
does, so a recording has the guest's sounds with stretches of silence between
them. The checks allow for that: a tone counts if some window of it stands out
at its frequency, and --max-gap is measured only between the first and last
loud windows.

  wavcheck.py FILE --tone 440 [--tone 1000 ...] [--min-ratio 8] [--max-gap MS]

Exit status 0 when every tone is found (and the gap limit holds), 1 otherwise.
"""
import argparse
import math
import struct
import sys


def read_mono(path):
    """Samples as floats in -1..1, mixed to mono, and the rate. Reads the RIFF
    chunks itself: OpenAL Soft may write WAVE_FORMAT_EXTENSIBLE, and float
    samples, which Python's wave module refuses."""
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("wavcheck: %s: not a RIFF WAVE file" % path)
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        raise SystemExit("wavcheck: %s: no fmt or data chunk" % path)
    tag, ch, rate = struct.unpack("<HHI", fmt[:8])
    bits = struct.unpack("<H", fmt[14:16])[0]
    if tag == 0xFFFE:                            # extensible: the sub-format's first two bytes
        tag = struct.unpack("<H", fmt[24:26])[0]
    if tag == 1 and bits == 16:
        vals, scale = struct.unpack("<%dh" % (len(raw) // 2), raw[:len(raw) // 2 * 2]), 32768.0
    elif tag == 1 and bits == 8:
        vals, scale = [v - 128 for v in raw], 128.0
    elif tag == 3 and bits == 32:
        vals, scale = struct.unpack("<%df" % (len(raw) // 4), raw[:len(raw) // 4 * 4]), 1.0
    else:
        raise SystemExit("wavcheck: format %d with %d-bit samples not supported" % (tag, bits))
    mono = [sum(vals[i:i + ch]) / (ch * scale) for i in range(0, len(vals) - ch + 1, ch)]
    return mono, rate


def goertzel(x, rate, freq):
    k = 2.0 * math.cos(2.0 * math.pi * freq / rate)
    s1 = s2 = 0.0
    for v in x:
        s1, s2 = v + k * s1 - s2, s1
    return s1 * s1 + s2 * s2 - k * s1 * s2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--tone", type=float, action="append", default=[])
    ap.add_argument("--min-ratio", type=float, default=8.0,
                    help="tone power over the mean of two neighbour frequencies")
    ap.add_argument("--max-gap", type=float, default=0, help="ms; 0 = not checked")
    ap.add_argument("--window-ms", type=float, default=50)
    a = ap.parse_args()
    x, rate = read_mono(a.wav)
    win = max(64, int(rate * a.window_ms / 1000))
    wins = [x[i:i + win] for i in range(0, len(x) - win + 1, win)]
    rms = [math.sqrt(sum(v * v for v in w) / len(w)) for w in wins]
    loud = [i for i, r in enumerate(rms) if r > 0.01]
    print("wavcheck: %s: %.2f s at %d Hz, %d of %d windows loud" % (a.wav, len(x) / rate, rate, len(loud), len(wins)))
    ok = bool(loud)
    for f in a.tone:
        best = 0.0
        for i in loud:
            p = goertzel(wins[i], rate, f)
            q = (goertzel(wins[i], rate, f * 0.8) + goertzel(wins[i], rate, f * 1.25)) / 2 + 1e-12
            best = max(best, p / q)
        found = best >= a.min_ratio
        print("wavcheck: tone %g Hz: best ratio %.1f (%s)" % (f, best, "found" if found else "missing"))
        ok &= found
    if a.max_gap and loud:
        gap, run = 0, 0
        for i in range(loud[0], loud[-1] + 1):
            run = run + 1 if rms[i] <= 0.01 else 0
            gap = max(gap, run)
        gap_ms = gap * a.window_ms
        print("wavcheck: longest quiet stretch inside the sound: %.0f ms (limit %.0f)" % (gap_ms, a.max_gap))
        ok &= gap_ms <= a.max_gap
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
