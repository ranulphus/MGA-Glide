#!/usr/bin/env python3
"""Check that Loop A's generated configuration has not changed by accident.

For each case below, `run.py --emit-config` prints the 86box.cfg it would
write (with placeholder paths), C:\\RUN.BAT and the golden image key, and the
output must match tools/loopa/ref/<case>.txt byte for byte. A deliberate
change is recorded with --update and reviewed in the diff. No emulator,
container or disk image is needed.

  python3 tools/loopa/cfgcheck.py            (make loopa-cfgcheck)
  python3 tools/loopa/cfgcheck.py --update   (rewrite the references)
"""
import argparse
import os
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
REF = os.path.join(HERE, "ref")

# name -> run.py arguments. The first cases pin what existing jobs generate
# and must never change without a deliberate --update.
CASES = {
    "default": "--exe build/ow/dos/HELLO.EXE",
    "g450-sound-mouse": "--exe build/ow/dos/CONFORM.EXE --args t04 --card g450 --sound sb16 --mouse ps2 "
                        "--mem 128 --pre 'SET BLASTER=A220 I5 D1 H5 T6'",
    "joystick": "--exe build/djgpp/JOYTEST.EXE --card g200 --keys @HX-TEST,1:joy:axis:0:0",
    "cmd": "--card g400 --voodoo 0 --cmd 'SERSAY HX-START x' --cmd 'SERSAY HX-DONE 0'",
    # Machine profiles and the generic VBE card.
    "486dx2": "--machine 486dx2 --exe build/ow/dos/HELLO.EXE",
    "486dx4": "--machine 486dx4 --voodoo 0 --exe build/ow/dos/HELLO.EXE",
    "bf6-vbe": "--card vbe --exe build/ow/dos/HELLO.EXE",
    # Network, COM2, a wrapped program and the interpreter instead of the dynarec.
    "net-com2": "--net ne2k --net-fwd 22 --net-fwd 5555:23 --net-dos --com2 --exe build/ow/dos/HELLO.EXE",
    "rtl8139": "--machine 486dx2 --net rtl8139c+ --exe build/ow/dos/HELLO.EXE",
    "wrap-dynarec": "--dynarec 0 --wrap 'C:\\GLOS\\GLOS.EXE /RUN' --exe build/ow/dos/HELLO.EXE",
    "himemx": "--boot-cfg himemx --machine 486dx4 --exe build/ow/dos/HELLO.EXE",
}


def emit(args):
    env = dict(os.environ)
    env.pop("MGA_CARD", None)
    cmd = [sys.executable, os.path.join(HERE, "run.py"), "--name", "cfgcheck", "--emit-config"] + shlex.split(args)
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True, env=env, cwd=ROOT).stdout


def emit_vpc():
    """tools/bench/vpc.py's configuration (it builds on run.py's)."""
    sys.path.insert(0, os.path.join(ROOT, "tools/bench"))
    import vpc
    L = vpc.loopa()
    with tempfile.TemporaryDirectory() as vm:
        path = vpc.config(L, "g200", vm, "@SERIAL@", "@CIMG@", "@BOOTIMG@")
        return open(path).read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    os.makedirs(REF, exist_ok=True)
    got = {name: emit(args) for name, args in CASES.items()}
    got["vpc-g200"] = emit_vpc()
    bad = 0
    for name, text in got.items():
        ref = os.path.join(REF, name + ".txt")
        if a.update:
            open(ref, "w").write(text)
            print("  cfgcheck %s: written" % name)
        elif not os.path.exists(ref) or open(ref).read() != text:
            print("  cfgcheck %s: DIFFERS from %s" % (name, os.path.relpath(ref, ROOT)))
            bad += 1
        else:
            print("  cfgcheck %s: same (ok)" % name)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
