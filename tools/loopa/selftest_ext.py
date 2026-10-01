#!/usr/bin/env python3
"""Loop A self-tests for the harness's machine options (make loopa-selftest-ext).

Each check runs one short job through run.py and looks at its result:

  486     HELLO (DOS/4GW) and STACKPG (DJGPP) on the 486dx2 and 486dx4 profiles
  vbe     VBEINFO on the S3 Trio64V2/DX: VBE 2.0 with linear 640x480x16

Runs inside the dev container (tools/dev). Arguments pick checks; the
default is all of them. Exit status 1 if any check fails.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "out")


def job(name, *args):
    """Run one Loop A job; returns (status, serial text)."""
    subprocess.run([sys.executable, os.path.join(HERE, "run.py"), "--name", name, "--idle", "40",
                    "--boot-grace", "60", "--timeout", "240"] + list(args),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
    d = os.path.join(OUT, name)
    status = open(os.path.join(d, "status")).read().strip() if os.path.exists(os.path.join(d, "status")) else "?"
    serial = open(os.path.join(d, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(d, "serial.log")) else ""
    return status, serial


def report(name, ok, detail):
    print("  selftest %s: %s%s" % (name, detail, " (ok)" if ok else " (FAILED)"), flush=True)
    return ok


def check_486():
    ok = True
    for m in ("486dx2", "486dx4"):
        st, _ = job("selftest-%s-hello" % m, "--machine", m, "--exe", "build/ow/dos/HELLO.EXE")
        ok &= report("%s-hello" % m, st == "PASS", "HELLO (DOS/4GW) %s" % st)
        st, _ = job("selftest-%s-stackpg" % m, "--machine", m, "--exe", "build/djgpp/STACKPG.EXE")
        ok &= report("%s-stackpg" % m, st == "PASS", "STACKPG (DJGPP, CWSDPMI) %s" % st)
    return ok


def check_vbe():
    ok = True
    for m in ("bf6", "486dx2"):
        st, serial = job("selftest-vbe-%s" % m, "--machine", m, "--card", "vbe", "--voodoo", "0",
                         "--file", "build/ow/dos/VBEINFO.COM=/HX/VBEINFO.COM",
                         "--cmd", "SERSAY HX-START vbe", "--cmd", "VBEINFO", "--cmd", "SERSAY HX-DONE 0")
        line = next((l for l in serial.split("\n") if l.startswith("HX-VBE ")), "no HX-VBE line")
        good = st == "PASS" and "ver=0200" in line and "m640x480x16=none" not in line and " lfb=0" not in line
        ok &= report("vbe-%s" % m, good, "%s: %s" % (st, line))
    return ok


CHECKS = {"486": check_486, "vbe": check_vbe}


def main():
    names = sys.argv[1:] or list(CHECKS)
    bad = [n for n in names if not CHECKS[n]()]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
