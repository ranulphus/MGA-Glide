#!/usr/bin/env python3
"""Loop B push-and-run: publish a job for a bench PC and collect results.

Runs on the build host itself (not in the dev container: the PCs must reach
its HTTP and FTP ports). The PC's BENCH.BAT poller fetches
dist/bench/<pc>/FETCH.BAT when LATEST.TXT changes, runs TEST\\RUN.BAT,
uploads C:\\OUT to this script's FTP sink and reboots.

  run.py --pc bench-g200 --exe build/ow/dos/PROBE.EXE
  run.py --pc bench-g200 --exe build/ow/dos/CONFORM.EXE --ovl build/ow/GLIDE2X.OVL --args "t04"
  run.py --pc bench-g200 --replay TRACE.BIN --frames 60,150 --ovl build/ow/GLIDE2X.OVL
  run.py --pc bench-g200 --game gta --ovl build/ow/GLIDE2X.OVL --set MGAGLIDE_EXIT=600
  run.py --pc bench-g450 --file A.EXE --file B.EXE --cmd "A.EXE --x" --cmd "B.EXE --y"

--cmd runs several programs in one job (DOSBench runs its Glide and OpenGL
programs back to back): each line goes into RUN.BAT in turn, every EXE
shipped with --file gets its DOS extender, and the job passes when every
program reports HX-DONE 0.

PCs and their serial ports, capture devices and reset hooks come from
tools/bench/bench.toml and bench.local.toml. Results: out/bench/<pc>/<job>/
(serial.log, files/ = the PC's C:\\OUT, *.png, capture-*.png, result.json).

Statuses: PASS FAIL CRASH HANG TIMEOUT NOT-PICKED-UP.
"""
import argparse
import functools
import http.server
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "loopa"))
import capture  # noqa: E402
import ftpsink  # noqa: E402
import png  # noqa: E402
import serlog  # noqa: E402

WATCOM = os.environ.get("WATCOM", os.path.expanduser("~/.local/opt/watcom-20260901"))
DJGPP_PREFIX = os.environ.get("DJGPP_PREFIX", os.path.expanduser("~/.local/opt/djgpp-gcc1220"))
EXTENDERS = {"dos4gw": (os.path.join(WATCOM, "binw/dos4gw.exe"), "DOS4GW.EXE"),
             "cwsdpmi": (os.path.join(DJGPP_PREFIX, "dos/CWSDPMI.EXE"), "CWSDPMI.EXE")}


def extender_of(exe):
    """DJGPP programs carry the go32 stub and need CWSDPMI; the rest DOS/4GW."""
    with open(exe, "rb") as f:
        head = f.read(4096)
    return "cwsdpmi" if b"go32stub" in head or b"CWSDPMI" in head else "dos4gw"
DOS83 = re.compile(r"^[A-Z0-9_$~!#%&-]{1,8}(\.[A-Z0-9_$~!#%&-]{1,3})?$")


def load_config():
    cfg = tomllib.load(open(os.path.join(HERE, "bench.toml"), "rb"))
    local = os.path.join(HERE, "bench.local.toml")
    if os.path.exists(local):
        over = tomllib.load(open(local, "rb"))
        cfg["host"].update(over.get("host", {}))
        for pc, vals in over.get("pc", {}).items():
            cfg["pc"].setdefault(pc, {}).update(vals)
    return cfg


def ensure_http(port, root):
    """Serve dist/bench for the job unless tools/bench/serve.sh already is."""
    try:
        socket.create_connection(("127.0.0.1", port), timeout=1).close()
        return None
    except OSError:
        pass
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    handler = functools.partial(Quiet, directory=root)
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def dos_lines(lines):
    return ("\r\n".join(["@ECHO OFF"] + lines) + "\r\n").encode("ascii")


class Capture:
    """Grab single frames from the PC's capture device on HX-CAPTURE."""

    def __init__(self, dev, opts, out):
        self.dev, self.opts, self.out, self.threads = dev, opts, out, []

    def grab(self, name):
        if not self.dev:
            return
        dst = os.path.join(self.out, "capture-%s.png" % re.sub(r"[^\w.-]", "_", name))
        cmd = capture.grab_cmd(self.dev, dst, self.opts)
        t = threading.Thread(target=subprocess.call, args=(cmd,), daemon=True)
        t.start()
        self.threads.append(t)

    def wait(self):
        for t in self.threads:
            t.join(30)


def build_job(a, pc, cfg, job, token, jd):
    """Write the job directory, RUN.BAT, UP.TXT; return the file names."""
    shutil.rmtree(jd, ignore_errors=True)
    os.makedirs(jd)
    files = []

    def add(src, name=None):
        name = (name or os.path.basename(src)).upper()
        if not DOS83.match(name):
            sys.exit("bench: %s is not a DOS 8.3 name" % name)
        shutil.copyfile(src, os.path.join(jd, name))
        files.append(name)
        return name

    exts = [a.extender] if a.extender else []
    if not exts:
        exes = ([a.exe] if a.exe else []) + [f.partition("=")[0] for f in a.file
                                             if (f.partition("=")[2] or f).upper().endswith(".EXE")]
        exts = sorted({extender_of(e) for e in exes}) or ["dos4gw"]
    for ext in exts:
        add(*EXTENDERS[ext])
    if a.ovl:
        add(a.ovl, "GLIDE2X.OVL")
    for spec in a.file:
        src, _, name = spec.partition("=")
        add(src, name or None)
    host = cfg["host"]
    addr = pc.get("address", host["address"])
    run = ["C:", "CD \\TEST", "IF NOT EXIST C:\\OUT\\NUL MD C:\\OUT",
           "ECHO Y| DEL C:\\OUT\\*.* > NUL", "SET HX_CAPWAIT=%d" % a.capwait]
    run += ["SET %s" % s for s in a.set]
    run += ["SERSAY HX-JOB %s" % job]
    if a.game:
        game = json.load(open(os.path.join(ROOT, "tools/games/games.json")))[a.game]
        gdir = pc["games"].rstrip("\\") + "\\" + game["cwd"]
        ovl = pc["games"].rstrip("\\") + "\\" + game["ovl"]
        run += [gdir[:2], "CD %s" % gdir[2:],
                "IF NOT EXIST %s.MGB COPY %s %s.MGB > NUL" % (ovl[:-4], ovl, ovl[:-4])]
        if a.ovl:
            run += ["COPY C:\\TEST\\GLIDE2X.OVL %s > NUL" % ovl]
        run += [l + (" " + a.args if a.args else "") for l in game["run"]]
        run += ["COPY %s.MGB %s > NUL" % (ovl[:-4], ovl), "C:", "CD \\TEST"]
    elif a.cmd:
        run += a.cmd
    else:
        run += ["%s %s" % (add(a.exe), a.args)]
    run += ["SERSAY HX-EXIT",
            "COPY C:\\TEST\\UP.TXT C:\\HX\\UP.TXT > NUL",
            "FOR %%F IN (C:\\OUT\\*.*) DO ECHO put %%F>> C:\\HX\\UP.TXT",
            "ECHO quit>> C:\\HX\\UP.TXT",
            "SERSAY HX-UPLOAD",
            "FTP -port %d %s < C:\\HX\\UP.TXT > C:\\HX\\FTP.LOG" % (pc["ftp_port"], addr),
            "SERSAY HX-UPLOAD-END"]
    open(os.path.join(jd, "RUN.BAT"), "wb").write(dos_lines(run))
    open(os.path.join(jd, "UP.TXT"), "wb").write(("bench\r\n%s\r\n" % token).encode())
    return files + ["RUN.BAT", "UP.TXT"]


def main():
    cfg = load_config()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pc", required=True, choices=sorted(cfg["pc"]))
    ap.add_argument("--name")
    ap.add_argument("--exe")
    ap.add_argument("--args", default="")
    ap.add_argument("--ovl")
    ap.add_argument("--file", action="append", default=[], help="SRC[=NAME.EXT] extra file for C:\\TEST")
    ap.add_argument("--set", action="append", default=[], help="VAR=VALUE environment for the job")
    ap.add_argument("--replay", help="GLPLAY a call trace (tools/gltrace) with --ovl")
    ap.add_argument("--frames", default="", help="frames GLPLAY captures (with --replay)")
    ap.add_argument("--game", help="game key from tools/games/games.json, installed on the PC")
    ap.add_argument("--cmd", action="append", default=[], help="RUN.BAT line (repeatable) instead of --exe")
    ap.add_argument("--timeout", type=float, default=900, help="seconds from pick-up to HX-EXIT")
    ap.add_argument("--pickup", type=float, default=300, help="seconds for the PC to pick the job up")
    ap.add_argument("--idle", type=float, help="silent seconds that count as a hang (default 180; games "
                    "log nothing per frame, so for --game only --timeout applies)")
    ap.add_argument("--capwait", type=int, default=4, help="seconds the PC holds a frame on HX-CAPTURE")
    ap.add_argument("--extender", choices=sorted(EXTENDERS),
                    help="DPMI host shipped with the job (default: CWSDPMI for DJGPP programs, else DOS/4GW)")
    a = ap.parse_args()
    if a.replay:
        a.exe = a.exe or os.path.join(ROOT, "build/ow/dos/GLPLAY.EXE")
        a.file.append("%s=TRACE.BIN" % a.replay)
        a.args = ("C:\\TEST\\TRACE.BIN %s %s" % (a.frames, a.args)).strip()
    if a.idle is None:
        a.idle = a.timeout if a.game else 180
    if not (a.exe or a.game or a.cmd):
        ap.error("one of --exe, --replay, --game, --cmd is needed")
    pc = cfg["pc"][a.pc]
    host = cfg["host"]
    job = "%s-%d" % (a.name or a.game or (os.path.basename(a.exe).split(".")[0].lower() if a.exe else "cmd"),
                     int(time.time()))
    token = secrets.token_hex(6)
    pub = os.path.join(ROOT, "dist/bench", a.pc)
    out = os.path.join(ROOT, "out/bench", a.pc, job)
    os.makedirs(out, exist_ok=True)
    names = build_job(a, pc, cfg, job, token, os.path.join(pub, "job"))
    fetch = ["IF NOT EXIST C:\\TEST\\NUL MD C:\\TEST", "ECHO Y| DEL C:\\TEST\\*.* > NUL"]
    # HTGET exits 20-29 on a 2xx reply. Each file gets three tries: a fetch
    # straight after another one occasionally fails at the socket level.
    for i, n in enumerate(names):
        fetch += ["SET HX_TRY=",
                  ":F%d" % i,
                  "SET HX_TRY=%HX_TRY%x",
                  "HTGET -o C:\\TEST\\%s %%HX_URL%%/job/%s > NUL" % (n, n),
                  "IF ERRORLEVEL 30 GOTO R%d" % i,
                  "IF ERRORLEVEL 20 GOTO K%d" % i,
                  ":R%d" % i,
                  "IF NOT \"%%HX_TRY%%\"==\"xxx\" GOTO F%d" % i,
                  "SERSAY HX-FETCH-FAIL %s" % n,
                  ":K%d" % i]
    open(os.path.join(pub, "FETCH.BAT"), "wb").write(dos_lines(fetch))

    http_srv = ensure_http(host["http_port"], os.path.join(ROOT, "dist/bench"))
    # PASV replies name the address the PC used; behind SLiRP that is the
    # PC's own view of the host (pc.address), not the socket's local one.
    sink = ftpsink.FtpSink(os.path.join(out, "files"), pc["ftp_port"], pc.get("address"),
                           "bench", token).start()
    lines = serlog.Lines(pc["serial"], os.path.join(ROOT, "out/bench", a.pc, "serial.log"))
    cap = Capture(pc.get("capture", ""), pc.get("capture_opts", ""), out)
    open(os.path.join(pub, "LATEST.TXT"), "w").write(job + "\n")
    print("bench: published %s for %s (%s); waiting on %s" % (job, a.pc, pc["card"], pc["serial"]), flush=True)

    log = open(os.path.join(out, "serial.log"), "w")
    t0 = time.time()
    picked = exited = upload_end = done = None
    last, status, game_exit = t0, None, False
    while True:
        now = time.time()
        got = lines.poll()
        for text in got:
            last = now
            if text.startswith("HX-FETCH-FAIL"):
                print(text, flush=True)
            if picked is None:
                if text.startswith("HX-JOB ") and text.split()[1:2] == [job]:
                    picked = now
                    print("bench: picked up", flush=True)
                continue
            log.write(text + "\n")
            log.flush()
            print(text, flush=True)
            if text.startswith("HX-CAPTURE "):
                cap.grab(text.split(None, 1)[1])
            elif text.startswith("HX-DONE "):
                # Every program must pass (--cmd jobs run several).
                done = (done is not False) and text.split()[1:2] == ["0"]
            elif text.startswith("MGL-EXIT frames="):
                game_exit = True
            elif text.startswith("HX-EXIT"):
                exited = now
            elif text.startswith("HX-UPLOAD-END"):
                upload_end = now
            elif text.startswith("HX-JOB-END") or text.startswith("HX-BOOT"):
                upload_end = upload_end or now
        if upload_end or (exited and now - exited > 300):
            break
        if picked is None and now - t0 > a.pickup:
            status = "NOT-PICKED-UP"
            break
        if picked and not exited and now - picked > a.timeout:
            status = "TIMEOUT"
            break
        if picked and not exited and now - last > a.idle:
            status = "HANG"
            break
        if not got:
            time.sleep(0.05)
    if status is None:
        if a.game:
            status = "PASS" if game_exit else "CRASH"
        else:
            status = {True: "PASS", False: "FAIL", None: "CRASH"}[done]
    if status in ("HANG", "TIMEOUT") and pc.get("reset"):
        print("bench: resetting %s: %s" % (a.pc, pc["reset"]), flush=True)
        subprocess.call(pc["reset"], shell=True, cwd=ROOT)
    cap.wait()
    sink.stop()
    if http_srv:
        http_srv.shutdown()
    files = os.path.join(out, "files")
    for fn in sorted(os.listdir(files)):
        if fn.upper().endswith(".PPM"):
            w, h, rgb = png.read_ppm(os.path.join(files, fn))
            png.write_png(os.path.join(out, fn[:-4].lower() + ".png"), w, h, rgb)
    result = {"job": job, "pc": a.pc, "card": pc["card"], "status": status,
              "exe": a.exe, "args": a.args, "ovl": a.ovl, "game": a.game, "cmd": a.cmd,
              "uploaded": sink.received, "upload_complete": bool(upload_end),
              "elapsed_s": round(time.time() - t0, 1)}
    json.dump(result, open(os.path.join(out, "result.json"), "w"), indent=1)
    print("bench: %s %s (%d files uploaded) -> %s" % (job, status, len(sink.received), out), flush=True)
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
