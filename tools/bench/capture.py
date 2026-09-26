#!/usr/bin/env python3
"""Video capture for Loop B (a V4L2 device such as the Epiphan DVI2USB 3.0,
which is a UVC device on Linux). Needs ffmpeg and membership of the
`video` group.

  capture.py list                         V4L2 devices and their names
  capture.py formats /dev/video0          pixel formats and frame sizes
  capture.py grab /dev/video0 out.png [--opts "-input_format yuyv422"]
                                          one frame, after the device settles

The capture device follows the card's output mode, so a grab reports the
size it saw: 720x400 for DOS text mode (70 Hz), 640x480 and up for the
runtime's modes. tools/bench/run.py grabs through grab_cmd() on each
HX-CAPTURE line, with the PC's capture_opts from bench.toml.
"""
import argparse
import glob
import os
import subprocess
import sys


def grab_cmd(dev, dst, opts=""):
    # Skip the first frames: a UVC device may still deliver the previous
    # mode's picture right after the stream starts.
    return (["ffmpeg", "-loglevel", "error", "-y", "-f", "v4l2"] + opts.split() +
            ["-i", dev, "-vf", "select=gte(n\\,5)", "-frames:v", "1", dst])


def size_of(png):
    with open(png, "rb") as f:
        head = f.read(24)
    return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    f = sub.add_parser("formats")
    f.add_argument("dev")
    g = sub.add_parser("grab")
    g.add_argument("dev")
    g.add_argument("out")
    g.add_argument("--opts", default="")
    a = ap.parse_args()
    if a.cmd == "list":
        devs = sorted(glob.glob("/sys/class/video4linux/video*"))
        if not devs:
            print("no V4L2 devices (is the capture device plugged in?)")
            return 1
        for d in devs:
            name = open(os.path.join(d, "name")).read().strip()
            node = "/dev/" + os.path.basename(d)
            ok = "ok" if os.access(node, os.R_OK | os.W_OK) else "no access (video group?)"
            print("%-12s %-40s %s" % (node, name, ok))
        return 0
    if a.cmd == "formats":
        r = subprocess.run(["ffmpeg", "-hide_banner", "-f", "v4l2", "-list_formats", "all", "-i", a.dev],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print("\n".join(l for l in r.stdout.splitlines() if "v4l2" in l or "Raw" in l or "Compressed" in l))
        return 0
    rc = subprocess.call(grab_cmd(a.dev, a.out, a.opts))
    if rc or not os.path.exists(a.out):
        print("grab failed (%d)" % rc)
        return 1
    w, h = size_of(a.out)
    print("%s: %dx%d" % (a.out, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
