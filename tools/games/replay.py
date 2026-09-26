#!/usr/bin/env python3
"""REPLAY check (PRD §11.6): replay one recorded game trace through the
retail OVL on the emulated Voodoo and through MGA-Glide on the G100, and
compare the captured frames.

  replay.py TRACE --ovl-ref RETAIL.OVL --frames 100,200,300 [--name sr]
            [--tol 40 --frac 0.05 --box]

Game frames are game content: the outputs stay under out/replay/<name>/
and are never committed. Exit status 0 when every frame compares within
the tolerances."""
import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import imgcmp  # noqa: E402

EXE = os.path.join(ROOT, "build", "ow", "dos", "GLPLAY.EXE")
MGA_OVL = os.path.join(ROOT, "build", "ow", "GLIDE2X.OVL")


def run(name, trace, ovl, frames, outdir):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "loopa", "run.py"), "--name", name,
           "--exe", EXE, "--args", "C:\\TEST\\TRACE.BIN %s" % frames, "--ovl", ovl,
           "--file", "%s=/TEST/TRACE.BIN" % trace, "--out", outdir,
           "--timeout", "1500", "--idle", "600"]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return json.load(open(os.path.join(outdir, "result.json")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--ovl-ref", required=True)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--name", default="game")
    ap.add_argument("--tol", type=int, default=40)
    ap.add_argument("--frac", type=float, default=0.05)
    ap.add_argument("--box", action="store_true")
    a = ap.parse_args()
    base = os.path.join(ROOT, "out", "replay", a.name)
    dirs = {"ref": os.path.join(base, "ref"), "mga": os.path.join(base, "mga")}
    with cf.ThreadPoolExecutor(2) as ex:
        futs = {k: ex.submit(run, "replay-%s-%s" % (a.name, k), a.trace,
                             a.ovl_ref if k == "ref" else MGA_OVL, a.frames, d)
                for k, d in dirs.items()}
        res = {k: f.result() for k, f in futs.items()}
    report = {"runs": {k: r["status"] for k, r in res.items()},
              "stats": {k: r.get("stats") for k, r in res.items()}, "frames": {}}
    ok = all(r["status"] == "PASS" for r in res.values())
    for f in a.frames.split(","):
        fn = "glp_%s.png" % f.strip()
        ref, got = os.path.join(dirs["ref"], fn), os.path.join(dirs["mga"], fn)
        if not (os.path.exists(ref) and os.path.exists(got)):
            report["frames"][fn] = {"ok": False, "reason": "missing"}
            ok = False
            continue
        c = imgcmp.compare(ref, got, a.tol, a.frac, True, a.box, os.path.join(base, fn[:-4] + ".diff.png"))
        report["frames"][fn] = c
        ok = ok and c["ok"]
    report["ok"] = ok
    json.dump(report, open(os.path.join(base, "report.json"), "w"), indent=1)
    for fn, c in report["frames"].items():
        print("replay %-8s %-14s %s frac=%s" % (a.name, fn, "PASS" if c.get("ok") else "FAIL", c.get("frac")))
    print("replay %s: %s (runs: %s)" % (a.name, "PASS" if ok else "FAIL", report["runs"]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
