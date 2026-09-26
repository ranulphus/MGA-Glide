#!/usr/bin/env python3
"""Loop B push-and-run: publish a job for a bench PC and collect results.

The PC's BENCH.BAT poller fetches dist/bench/<pc>/FETCH.BAT when
LATEST.TXT changes, runs JOB\\RUN.BAT and reboots.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PCS = {"bench-g100": "/dev/ttyS0", "bench-g200": "/dev/ttyS1", "bench-g450": "/dev/ttyS2"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pc", required=True, choices=sorted(PCS))
    ap.add_argument("--name")
    ap.add_argument("--exe", required=True)
    ap.add_argument("--args", default="")
    ap.add_argument("--ovl")
    ap.add_argument("--timeout", type=float, default=600)
    ap.add_argument("--video", default=os.environ.get("BENCH_VIDEO", ""))
    a = ap.parse_args()
    job = "%s-%d" % (a.name or os.path.basename(a.exe).lower(), int(time.time()))
    pub = os.path.join(ROOT, "dist/bench", a.pc)
    jd = os.path.join(pub, "job")
    shutil.rmtree(jd, ignore_errors=True)
    os.makedirs(jd)
    files = [a.exe] + ([a.ovl] if a.ovl else []) + [os.path.join(os.environ.get("WATCOM", ""), "binw/dos4gw.exe")]
    names = []
    for f in files:
        n = os.path.basename(f).upper()
        n = "GLIDE2X.OVL" if f == a.ovl else n
        shutil.copyfile(f, os.path.join(jd, n))
        names.append(n)
    exe = os.path.basename(a.exe).upper()
    run = ["@ECHO OFF", "CD \\HX\\JOB", "SERSAY HX-JOB %s" % job,
           "%s %s" % (exe, a.args), "SERSAY HX-EXIT"]
    open(os.path.join(jd, "RUN.BAT"), "w", newline="\r\n").write("\n".join(run) + "\n")
    fetch = ["@ECHO OFF", "IF NOT EXIST C:\\HX\\JOB\\NUL MD C:\\HX\\JOB"]
    fetch += ["HTGET -o C:\\HX\\JOB\\%s %%HX_URL%%/job/%s > NUL" % (n, n) for n in names + ["RUN.BAT"]]
    open(os.path.join(pub, "FETCH.BAT"), "w", newline="\r\n").write("\n".join(fetch) + "\n")
    open(os.path.join(pub, "LATEST.TXT"), "w").write(job + "\n")
    out = os.path.join(ROOT, "out/bench", a.pc, job)
    os.makedirs(out, exist_ok=True)
    print("bench: published %s for %s; waiting on %s" % (job, a.pc, PCS[a.pc]))
    rc = subprocess.call([sys.executable, os.path.join(ROOT, "tools/bench/serlog.py"), "--port", PCS[a.pc],
                          "--watch", exe.split(".")[0].lower(), "--timeout", str(a.timeout),
                          "--copy-to", os.path.join(out, "serial.log")])
    status = {0: "PASS", 1: "FAIL", 124: "NOT-PICKED-UP", 125: "HANG"}.get(rc, "ERROR")
    if a.video:
        subprocess.call(["ffmpeg", "-loglevel", "error", "-y", "-f", "v4l2", "-i", a.video,
                         "-vf", "select=gte(n\\,5)", "-frames:v", "1", os.path.join(out, "capture.png")])
    json.dump({"job": job, "pc": a.pc, "status": status}, open(os.path.join(out, "result.json"), "w"))
    print("bench: %s %s" % (job, status))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
