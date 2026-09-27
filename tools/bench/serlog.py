#!/usr/bin/env python3
"""Read a bench PC's serial log (115200 8N1), timestamp and store it.

  serlog.py --port /dev/ttyS0                 follow forever
  serlog.py --port file:out/vbench/g200/serial.log
                                              follow a log file (86Box PC)
  serlog.py --port /dev/ttyS0 --watch JOB     exit 0 on "HX-DONE 0" after
                                              "HX-START ... JOB", 1 on other
                                              HX-DONE, 124 on timeout, 125 idle

tools/bench/run.py uses Lines() directly.
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


class Lines:
    """Non-blocking line reader over a serial device or a growing file
    ("file:PATH", read from its current end; a truncated file restarts)."""

    def __init__(self, spec, log=None):
        self.buf = b""
        self.log = open(log, "a") if log else None
        if spec.startswith("file:"):
            # Relative paths are relative to the harness root, where vpc.py writes.
            root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
            self.path = os.path.join(root, spec[5:])
            os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
            if not os.path.exists(self.path):
                open(self.path, "wb").close()
            self.f = open(self.path, "rb")
            self.f.seek(0, os.SEEK_END)
            self.fd = None
        else:
            self.path, self.f = None, None
            self.fd = open_port(spec)

    def _read(self):
        if self.fd is not None:
            try:
                return os.read(self.fd, 4096)
            except BlockingIOError:
                return b""
        if os.path.getsize(self.path) < self.f.tell():
            self.f.seek(0)
        return self.f.read(65536)

    def poll(self):
        """Return the complete lines received since the last call."""
        self.buf += self._read()
        out = []
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            text = line.decode("latin-1").rstrip("\r")
            if self.log:
                self.log.write("%s %s\n" % (time.strftime("%H:%M:%S"), text))
                self.log.flush()
            out.append(text)
        return out


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
    src = Lines(a.port, a.log)
    copy = open(a.copy_to, "w") if a.copy_to else None
    t0, last, started = time.time(), time.time(), a.watch is None
    while True:
        lines = src.poll()
        now = time.time()
        for text in lines:
            last = now
            print(text, flush=True)
            if a.watch and text.startswith("HX-START") and a.watch in text:
                started = True
            if started and copy:
                copy.write(text + "\n")
                copy.flush()
            if a.watch and started and text.startswith("HX-DONE"):
                return 0 if text.split()[1:2] == ["0"] else 1
        if not lines:
            if a.watch and now - t0 > a.timeout:
                return 124
            if a.watch and started and now - last > a.idle:
                return 125
            time.sleep(0.05)


if __name__ == "__main__":
    sys.exit(main())
