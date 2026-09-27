#!/usr/bin/env python3
"""Loop A: run one DOS test program in 86Box (G100 + optional Voodoo).

Runs inside the dev container. Example:
  run.py --name hello --exe build/ow/dos/HELLO.EXE --args "--fail"
  run.py --name t02 --exe build/ow/dos/T02.EXE --ovl build/ow/GLIDE2X.OVL

Result directory out/<name>/: serial.log, 86box.log, stderr.log, files/
(the guest's C:\\OUT), *.png, result.json, status.

Statuses: PASS FAIL TIMEOUT HANG GUEST-EXC CRASH EMU-FATAL SETUP-ERROR.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import png  # noqa: E402

CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))
BOX86_DIR = os.environ.get("BOX86_DIR", os.path.join(CACHE, "86box"))
WATCOM = os.environ.get("WATCOM", os.path.expanduser("~/.local/opt/watcom-20260901"))



# Matrox cards the harness can fit: 86Box internal name, config section.
CARDS = {
    "g100": ("productiva_g100", "Matrox Productiva G100"),
    "g200": ("millennium_g200", "Matrox Millennium G200 (MGA-Glide emulation)"),
    "g400": ("millennium_g400", "Matrox Millennium G400 (MGA-Glide emulation)"),
    "g450": ("millennium_g450", "Matrox Millennium G450 (MGA-Glide emulation)"),
}

def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


class Disk:
    """mtools access to a partitioned hard-disk image."""

    def __init__(self, img, tmp, letter="c"):
        self.letter = letter
        self.rc = os.path.join(tmp, "mtoolsrc.%s" % letter)
        with open(self.rc, "w") as f:
            f.write('drive %s: file="%s" partition=1\nmtools_skip_check=1\n' % (letter, img))
        self.env = dict(os.environ, MTOOLSRC=self.rc)

    def mkdir(self, path):
        subprocess.run(["mmd", "-D", "s", self.letter + ":" + path], env=self.env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def put(self, src, dst):
        sh(["mcopy", "-o", "-D", "o", src, self.letter + ":" + dst], env=self.env)

    def get_dir(self, src, dst):
        os.makedirs(dst, exist_ok=True)
        subprocess.run(["mcopy", "-s", "-n", "-o", self.letter + ":" + src + "/*", dst + "/"], env=self.env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def start_xvfb(errf):
    """Start a private Xvfb on a free display; returns (process, ':N')."""
    r, w = os.pipe()
    x = subprocess.Popen(["Xvfb", "-displayfd", str(w), "-screen", "0", "1280x1024x24", "-nolisten", "tcp"],
                         pass_fds=(w,), stdout=errf, stderr=errf, start_new_session=True)
    os.close(w)
    num = b""
    while not num.endswith(b"\n"):
        c = os.read(r, 1)
        if not c:
            break
        num += c
    os.close(r)
    if not num.strip():
        raise RuntimeError("Xvfb did not start")
    return x, ":" + num.decode().strip()


def dos_bat(lines):
    return ("\r\n".join(["@ECHO OFF"] + lines) + "\r\n").encode("ascii")


def golden():
    out = subprocess.run([os.path.join(HERE, "mkgolden.sh")], check=True,
                         stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
    return out


def build_config(vm, a, serial, cimg, bootimg, extra_hdd):
    tpl = open(os.path.join(HERE, "86box.cfg.in")).read()
    subst = {
        "@RENDERER@": "sdl_software",
        "@VOODOO@": "1" if a.voodoo else "0",
        "@VOODOO_RECOMPILER@": str(a.voodoo_recompiler),
        "@VOODOO_THREADS@": str(a.voodoo_threads),
        "@G100_MB@": str(max(a.g100_mb, 16) if a.card in ("g400", "g450") else a.g100_mb),
        "@GFXCARD@": CARDS[a.card][0],
        "@GFXNAME@": CARDS[a.card][1],
        "@SNDCARD@": a.sound or "none",
        "@SERIAL@": serial,
        "@CIMG@": cimg,
        "@BOOTIMG@": bootimg,
        "@EXTRA_HDD@": extra_hdd,
    }
    for k, v in subst.items():
        tpl = tpl.replace(k, v)
    path = os.path.join(vm, "86box.cfg")
    open(path, "w").write(tpl)
    return path


def parse_serial(text):
    info = {"tests": [], "images": [], "stats": [], "done": None, "start": None, "lines": 0}
    for line in text.replace("\r", "").split("\n"):
        if not line:
            continue
        info["lines"] += 1
        if line.startswith("HX-START"):
            info["start"] = line
        elif line.startswith("HX-TEST "):
            parts = line.split(" ", 3)
            info["tests"].append({"name": parts[1], "result": parts[2] if len(parts) > 2 else "?",
                                  "detail": parts[3] if len(parts) > 3 else ""})
        elif line.startswith("HX-IMG "):
            info["images"].append(line[7:])
        elif line.startswith("HX-STAT "):
            info["stats"].append(line[8:])
        elif line.startswith("HX-DONE "):
            try:
                info["done"] = int(line.split()[1])
            except (IndexError, ValueError):
                info["done"] = -1
    return info


FATAL_RE = re.compile(r"(^|\s)(FATAL|fatal error|Fatal error)", re.M)


def run(a):
    out = os.path.abspath(a.out or os.path.join(ROOT, "out", a.name))
    shutil.rmtree(out, ignore_errors=True)
    vm = os.path.join(out, "vm")
    os.makedirs(vm)
    tmp = tempfile.mkdtemp(prefix="loopa-")
    result = {"name": a.name, "exe": a.exe, "args": a.args, "ovl": a.ovl, "status": "SETUP-ERROR"}
    try:
        gold = golden()
        bootimg = os.path.join(vm, "boot.img")
        cimg = os.path.join(vm, "c.img")
        shutil.copyfile(os.path.join(gold, "boot.img"), bootimg)
        sh(["cp", "--sparse=always", os.path.join(gold, "c.img"), cimg])
        d = Disk(cimg, tmp)
        for t in ("UTEXIT.COM", "SERSAY.COM", "WAITSEC.COM", "REBOOT.COM", "VMODE.COM"):
            d.put(os.path.join(ROOT, "build/ow/dos", t), "/HX/" + t)
        d.put(os.path.join(WATCOM, "binw/dos4gw.exe"), "/HX/DOS4GW.EXE")
        d.mkdir("/TEST")
        exe_name = os.path.basename(a.exe).upper() if a.exe else None
        if a.exe:
            d.put(a.exe, "/TEST/" + exe_name)
        game = None
        extra = ""
        if a.game:
            game = json.load(open(os.path.join(ROOT, "tools/games/games.json")))[a.game]
            fix = os.environ.get("FIXTURES_DIR", os.path.join(CACHE, "fixtures"))
            gimg_src = subprocess.run([os.path.join(ROOT, "tools/games/mkimage.sh"), a.game,
                                       os.path.join(fix, game["fixture"]), game["dir"]],
                                      check=True, stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
            gimg = os.path.join(vm, "game.img")
            sh(["cp", "--sparse=always", gimg_src, gimg])
            extra = ("hdd_02_parameters = 63, 16, 406, 0, ide\nhdd_02_fn = %s\n"
                     "hdd_02_ide_channel = 0:1\n" % gimg)
            gd = Disk(gimg, tmp, "d")
            if a.sound is None:
                a.sound = game.get("sound", "")
        for spec in a.file:
            src, dst = spec.split("=", 1) if "=" in spec else (spec, "/TEST/" + os.path.basename(spec).upper())
            d.put(src, dst.replace("\\", "/"))
        if a.ovl:
            import hashlib
            dst = a.ovl_dst or ("D:\\" + game["ovl"] if game else "C:\\TEST\\GLIDE2X.OVL")
            drive, path = dst[0].lower(), dst[2:].replace("\\", "/")
            (gd if drive == "d" else d).put(a.ovl, path)
            result["ovl_sha256"] = hashlib.sha256(open(a.ovl, "rb").read()).hexdigest()
            result["ovl_dst"] = dst
        run_lines = ["SET PATH=C:\\HX;A:\\FREEDOS\\BIN", "C:", "CD \\TEST",
                     "SERSAY HX-BOOT loop=A test=%s" % a.name]
        run_lines += a.pre
        if a.cmd:
            run_lines += a.cmd
        elif game:
            run_lines += ["D:", "CD \\" + game["cwd"]] + [l + (" " + a.args if a.args else "") for l in game["run"]]
            run_lines += ["C:", "SERSAY HX-GAME-EXIT"]
        else:
            run_lines += ["C:\\TEST\\%s %s" % (exe_name, a.args or "")]
        run_lines += ["VMODE", "SERSAY HX-EXIT program returned without ending the run",
                      "UTEXIT 124"]
        rb = os.path.join(tmp, "RUN.BAT")
        open(rb, "wb").write(dos_bat(run_lines))
        d.put(rb, "/RUN.BAT")

        serial = os.path.join(out, "serial.log")
        open(serial, "w").close()
        cfg = build_config(vm, a, serial, cimg, bootimg, extra)
        box = os.path.join(BOX86_DIR, "bin", "86Box")
        roms = os.path.join(BOX86_DIR, "roms")
        cmd = [box, "-P", vm, "-C", cfg, "-R", roms, "-N", "-L", os.path.join(out, "86box.log")]
        if os.environ.get("BOX86_GDB"):
            cmd = ["gdb", "-q", "-batch", "-ex", "handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint",
                   "-ex", "run", "-ex", "thread apply all bt 12", "--args"] + cmd
        nvr_cache = os.path.join(CACHE, "loopa", "nvr-bf6")
        if os.path.isdir(nvr_cache):
            shutil.copytree(nvr_cache, os.path.join(vm, "nvr"), dirs_exist_ok=True)
        errf = open(os.path.join(out, "stderr.log"), "wb")
        xvfb, display = start_xvfb(errf)
        env = dict(os.environ, BOX86_MONITOR="1", HOME=vm, XDG_CONFIG_HOME=vm,
                   SDL_AUDIODRIVER="dummy", DISPLAY=display)
        t0 = time.time()
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=errf, stderr=errf, env=env)
        last_size, last_change, status, done_at = 0, time.time(), None, None
        f1_taps, next_f1 = 0, t0 + 6
        boot_at = None
        events = []
        for k in filter(None, a.keys.split(",")):
            parts = k.split(":")
            mode = parts[2] if len(parts) > 2 else "tap"
            events.append((float(parts[0]), "key %s %s\n" % (mode, parts[1])))
        for sh_t in filter(None, a.shots.split(",")):
            events.append((float(sh_t), "screenshot\n"))
        events.sort()

        def console(cmd):
            try:
                p.stdin.write(cmd.encode())
                p.stdin.flush()
            except OSError:
                pass
        while True:
            rc = p.poll()
            size = os.path.getsize(serial)
            now = time.time()
            if size != last_size:
                last_size, last_change = size, now
                text = open(serial, "rb").read().decode("latin-1")
                if done_at is None and "HX-DONE" in text:
                    done_at = now
            if rc is not None:
                break
            if boot_at is None and size:
                boot_at = now
            while events and boot_at is not None and now - boot_at >= events[0][0]:
                console(events.pop(0)[1])
            if last_size == 0 and now >= next_f1 and f1_taps < 12:
                # A fresh NVRAM stops the BIOS at "press F1 to continue".
                try:
                    p.stdin.write(b"key tap 0x3b\n")
                    p.stdin.flush()
                except OSError:
                    pass
                f1_taps += 1
                next_f1 = now + 3
            if done_at and now - done_at > 15:
                status = "NO-EXIT"
                break
            if now - t0 > a.timeout:
                status = "TIMEOUT"
                break
            if now - last_change > a.idle and now - t0 > a.boot_grace:
                status = "HANG"
                break
            time.sleep(0.25)
        if p.poll() is None and status in ("HANG", "TIMEOUT"):
            # Leave a picture of the emulated display for diagnosis.
            try:
                shots = os.path.join(vm, "screenshots")
                p.stdin.write(b"screenshot\n")
                p.stdin.flush()
                for _ in range(40):
                    time.sleep(0.25)
                    if os.path.isdir(shots) and os.listdir(shots):
                        time.sleep(0.5)
                        break
            except OSError:
                pass
        if p.poll() is None:
            try:
                p.stdin.write(b"exit\n")
                p.stdin.flush()
            except OSError:
                pass
            for _ in range(40):
                if p.poll() is not None:
                    break
                time.sleep(0.25)
            if p.poll() is None:
                os.kill(p.pid, signal.SIGKILL)
                p.wait()
        xvfb.terminate()
        try:
            xvfb.wait(timeout=5)
        except subprocess.TimeoutExpired:
            xvfb.kill()
        errf.close()
        result["elapsed_s"] = round(time.time() - t0, 1)
        result["box_exit"] = p.returncode
        text = open(serial, "rb").read().decode("latin-1")
        info = parse_serial(text)
        result.update(info)
        stderr_text = open(os.path.join(out, "stderr.log"), "rb").read().decode("latin-1")
        result["mga_unsupported"] = sorted(set(re.findall(r"MGA-UNSUPPORTED: (.*)", stderr_text)))
        logpath = os.path.join(out, "86box.log")
        boxlog = open(logpath, "rb").read().decode("latin-1") if os.path.exists(logpath) else ""
        if status is None:
            if FATAL_RE.search(boxlog + stderr_text) and info["done"] is None:
                status = "EMU-FATAL"
            elif info["done"] is None and p.returncode == 0 and "MGL-EXIT frames=" in text:
                status = "PASS"        # a game ended by MGAGLIDE exit_after (no HX-DONE from games)
            elif info["done"] is None and p.returncode == 124:
                status = "GUEST-EXC"   # program died; RUN.BAT's fallback ended the run
            elif info["done"] is None:
                status = "CRASH"
            elif info["done"] == 0 and p.returncode in (0, 124):
                status = "PASS"        # 124: reported HX-DONE 0 but left ending the run to RUN.BAT
            else:
                status = "FAIL"
        elif status == "NO-EXIT":
            status = "PASS" if info["done"] == 0 else "FAIL"
            result["note"] = "unit tester exit did not end 86Box"
        result["status"] = status
        # Collect guest output files and convert images.
        files = os.path.join(out, "files")
        d.get_dir("/OUT", files)
        for fn in sorted(os.listdir(files)) if os.path.isdir(files) else []:
            if fn.upper().endswith(".PPM"):
                w, h, rgb = png.read_ppm(os.path.join(files, fn))
                png.write_png(os.path.join(out, fn[:-4].lower() + ".png"), w, h, rgb)
        vm_nvr = os.path.join(vm, "nvr")
        if status == "PASS" and not os.path.isdir(nvr_cache) and os.path.isdir(vm_nvr):
            shutil.copytree(vm_nvr, nvr_cache)
        result["f1_taps"] = f1_taps
        shots = os.path.join(vm, "screenshots")
        if os.path.isdir(shots):
            for i, fn in enumerate(sorted(os.listdir(shots))):
                shutil.copyfile(os.path.join(shots, fn), os.path.join(out, "screen-%d%s" % (i, os.path.splitext(fn)[1])))
        if not a.keep_vm:
            shutil.rmtree(vm, ignore_errors=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        with open(os.path.join(out, "result.json"), "w") as f:
            json.dump(result, f, indent=1)
        with open(os.path.join(out, "status"), "w") as f:
            f.write(result["status"] + "\n")
    print("%-24s %s (%ss)" % (a.name, result["status"], result.get("elapsed_s", "?")))
    return 0 if result["status"] == "PASS" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--exe", help="DOS program to copy to C:\\TEST and run")
    ap.add_argument("--cmd", action="append", default=[], help="RUN.BAT lines to run instead of --exe")
    ap.add_argument("--game", help="game key from tools/games/games.json (attached as D:)")
    ap.add_argument("--ovl-dst", help="DOS path for --ovl (default C:\\TEST\\GLIDE2X.OVL, or the game's)")
    ap.add_argument("--args", default="")
    ap.add_argument("--ovl", help="GLIDE2X.OVL to install as C:\\TEST\\GLIDE2X.OVL")
    ap.add_argument("--file", action="append", default=[], help="SRC[=/DOS/PATH] extra files")
    ap.add_argument("--pre", action="append", default=[], help="extra RUN.BAT lines before the test")
    ap.add_argument("--keys", default="", help="comma list of SECONDS:SCANCODE[:down|up] after HX-BOOT")
    ap.add_argument("--shots", default="", help="comma list of SECONDS after HX-BOOT to screenshot")
    ap.add_argument("--voodoo", type=int, default=1)
    ap.add_argument("--voodoo-threads", type=int, default=int(os.environ.get("VOODOO_THREADS", "1")))
    ap.add_argument("--voodoo-recompiler", type=int, default=int(os.environ.get("VOODOO_RECOMPILER", "0")))
    ap.add_argument("--g100-mb", type=int, default=8)
    ap.add_argument("--card", choices=sorted(CARDS), default=os.environ.get("MGA_CARD", "g100"),
                    help="Matrox card: g100, or g200 (the local emulation, patch 0004)")
    ap.add_argument("--sound", default=None)
    ap.add_argument("--timeout", type=float, default=float(os.environ.get("LOOPA_TIMEOUT", 300)))
    ap.add_argument("--idle", type=float, default=float(os.environ.get("LOOPA_IDLE", 60)))
    ap.add_argument("--boot-grace", type=float, default=45)
    ap.add_argument("--out")
    ap.add_argument("--keep-vm", action="store_true")
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()
