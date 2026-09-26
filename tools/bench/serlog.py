#!/usr/bin/env python3
"""Read a bench PC's serial log (115200 8N1), timestamp and store it.

  serlog.py --port /dev/ttyS0                 follow forever
  serlog.py --port /dev/ttyS0 --watch JOB     exit 0 on "HX-DONE 0" after
                                              "HX-START ... JOB", 1 on other
                                              HX-DONE, 124 on timeout, 125 idle
"""
import argparse
import os
import sys
import termios
import time


def open_port(path, baud=115200):
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, "B%d" % baud)
    attrs[0] = termios.IGNPAR                     # iflag
    attrs[1] = 0                                  # oflag
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] = 0                                  # lflag: raw
    attrs[4] = attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--log", default="out/bench/serial.log")
    ap.add_argument("--watch")
    ap.add_argument("--timeout", type=float, default=600)
    ap.add_argument("--idle", type=float, default=120)
    ap.add_argument("--copy-to")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.log), exist_ok=True)
    fd = open_port(a.port)
    log = open(a.log, "a")
    copy = open(a.copy_to, "w") if a.copy_to else None
    buf, t0, last, started = b"", time.time(), time.time(), a.watch is None
    while True:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            chunk = b""
        now = time.time()
        if chunk:
            last = now
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("latin-1").rstrip("\r")
                stamp = time.strftime("%H:%M:%S")
                log.write("%s %s\n" % (stamp, text))
                log.flush()
                print(text, flush=True)
                if a.watch and text.startswith("HX-START") and a.watch in text:
                    started = True
                if started and copy:
                    copy.write(text + "\n")
                    copy.flush()
                if a.watch and started and text.startswith("HX-DONE"):
                    return 0 if text.split()[1:2] == ["0"] else 1
        elif a.watch:
            if now - t0 > a.timeout:
                return 124
            if started and now - last > a.idle:
                return 125
            time.sleep(0.05)
        else:
            time.sleep(0.05)


if __name__ == "__main__":
    sys.exit(main())
