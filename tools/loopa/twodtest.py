#!/usr/bin/env python3
"""TWODTEST on every Matrox card (make loopa-twodtest; docs/matrox-2d.md):
the four AGP cards on the Pentium II, the PCI G200 and G450 on the 486DX2.
Three at a time. Arguments pick cards (g100 g200 g400 g450 g200pci g450pci).

Runs inside the dev container (tools/dev). Exit status 1 if any fails."""
import concurrent.futures as cf
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
RUNS = [("bf6", "g100"), ("bf6", "g200"), ("bf6", "g400"), ("bf6", "g450"), ("486dx2", "g200pci"),
        ("486dx2", "g450pci")]


def one(machine, card):
    name = "twodtest-%s" % card
    subprocess.run([sys.executable, os.path.join(HERE, "run.py"), "--name", name, "--machine", machine, "--card", card,
                    "--exe", "build/djgpp/TWODTEST.EXE", "--timeout", "1200", "--idle", "300"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
    d = os.path.join(ROOT, "out", name)
    status = open(os.path.join(d, "status")).read().strip() if os.path.exists(os.path.join(d, "status")) else "?"
    lines = []
    if os.path.exists(os.path.join(d, "serial.log")):
        lines = [l for l in open(os.path.join(d, "serial.log"), "rb").read().decode("latin-1").replace("\r", "").split("\n")
                 if l.startswith("HX-TEST ")]
    return machine, card, status, lines


def main():
    pick = sys.argv[1:]
    runs = [r for r in RUNS if not pick or r[1] in pick]
    bad = 0
    with cf.ThreadPoolExecutor(3) as ex:
        for machine, card, status, lines in ex.map(lambda r: one(*r), runs):
            failed = [l[len("HX-TEST "):] for l in lines if " FAIL" in l]
            print("  twodtest %-8s %-7s %s, %d cases%s" % (card, machine, status, len(lines),
                  "".join("\n    " + f for f in failed)), flush=True)
            bad += status != "PASS"
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
