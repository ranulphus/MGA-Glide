#!/usr/bin/env python3
"""Virtual bench PC: the Loop B dry run in 86Box.

An emulated PC provisioned like a real bench PC (docs/bench.md): NE2000 on
SLiRP networking with the Crynwr packet driver, mTCP, the HX tools and the
BENCH.BAT poller, COM1 to a log file, no unit tester (so snapshots take the
HX-CAPTURE path). It polls tools/bench/run.py's queue for pc vbench-<card>
and reboots after every job, like the real ones.

  MGA_DOCKER_NETWORK=host tools/dev python3 tools/bench/vpc.py start g200
  python3 tools/bench/run.py --pc vbench-g200 --exe build/ow/dos/PROBE.EXE
  python3 tools/bench/vpc.py reset g200     (run.py's reset hook)
  python3 tools/bench/vpc.py screenshot g200 (into out/vbench/g200/vm/screenshots/)
  python3 tools/bench/vpc.py stop g200

`start --game gta` also attaches that game's Loop A image (a local
fixture) as D:, where the vbench entries in bench.toml expect the games.
`start` runs in the foreground until `stop`. It needs the host's network
(MGA_DOCKER_NETWORK=host) so SLiRP's 10.0.2.2 reaches run.py's HTTP and
FTP ports on the host. State: out/vbench/<card>/ (serial.log, vm/).
"""
import argparse
import importlib.util
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
CACHE = os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide"))


def loopa():
    spec = importlib.util.spec_from_file_location("loopa_run", os.path.join(ROOT, "tools/loopa/run.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.join(ROOT, "tools/loopa"))
    spec.loader.exec_module(mod)
    return mod


def pins():
    text = open(os.path.join(ROOT, "tools/setup/versions.mk")).read()
    return dict(re.findall(r"^(\w+)\s*:=\s*(\S+)", text, re.M))


def fetch(url, sha, dest):
    subprocess.run([os.path.join(ROOT, "tools/setup/fetch.sh"), url, sha, dest], check=True)


def state_dir(card):
    return os.path.join(ROOT, "out", "vbench", card)


def provision(L, card, vm, tmp):
    """Fresh C: from the Loop A golden image, provisioned as a bench PC."""
    p = pins()
    gold = L.golden()
    bootimg, cimg = os.path.join(vm, "boot.img"), os.path.join(vm, "c.img")
    shutil.copyfile(os.path.join(gold, "boot.img"), bootimg)
    subprocess.run(["cp", "--sparse=always", os.path.join(gold, "c.img"), cimg], check=True)
    d = L.Disk(cimg, tmp)
    fetch(p["MTCP_URL"], p["MTCP_SHA256"], os.path.join(tmp, "mtcp.zip"))
    fetch(p["CRYNWR_URL"], p["CRYNWR_SHA256"], os.path.join(tmp, "crynwr.zip"))
    subprocess.run(["unzip", "-q", "-o", os.path.join(tmp, "mtcp.zip"), "dhcp.exe", "htget.exe", "ftp.exe",
                    "-d", os.path.join(tmp, "mtcp")], check=True)
    subprocess.run(["unzip", "-q", "-o", "-j", os.path.join(tmp, "crynwr.zip"), "DRIVERS/CRYNWR/NE2000.COM",
                    "-d", os.path.join(tmp, "pkt")], check=True)
    d.mkdir("/MTCP")
    d.mkdir("/PKTDRV")
    for f in os.listdir(os.path.join(tmp, "mtcp")):
        d.put(os.path.join(tmp, "mtcp", f), "/MTCP/" + f.upper())
    d.put(os.path.join(tmp, "pkt", "NE2000.COM"), "/PKTDRV/NE2000.COM")
    cfgf = os.path.join(tmp, "TCP.CFG")
    open(cfgf, "wb").write(b"PACKETINT 0x60\r\n")
    d.put(cfgf, "/MTCP/TCP.CFG")
    for t in ("SERSAY.COM", "WAITSEC.COM", "REBOOT.COM", "VMODE.COM", "UTEXIT.COM"):
        d.put(os.path.join(ROOT, "build/ow/dos", t), "/HX/" + t)
    d.put(os.path.join(HERE, "dos", "BENCH.BAT"), "/HX/BENCH.BAT")
    # The Loop A boot floppy's AUTOEXEC calls C:\RUN.BAT: here it is the
    # bench PC's AUTOEXEC tail (docs/bench.md, provisioning step 6).
    run = ["SET PATH=C:\\HX;C:\\MTCP;A:\\FREEDOS\\BIN", "C:",
           "C:\\PKTDRV\\NE2000 0x60 10 0x300 > NUL",
           "SET MTCPCFG=C:\\MTCP\\TCP.CFG",
           "SET HX_URL=http://10.0.2.2:%s/vbench-%s" % (os.environ.get("BENCH_HTTP_PORT", "8000"), card),
           "CALL C:\\HX\\BENCH.BAT"]
    rb = os.path.join(tmp, "RUN.BAT")
    open(rb, "wb").write(L.dos_bat(run))
    d.put(rb, "/RUN.BAT")
    return bootimg, cimg


def game_disk(game, vm):
    """The Loop A image of an installed game (local fixture), as D:."""
    import json
    g = json.load(open(os.path.join(ROOT, "tools/games/games.json")))[game]
    fix = os.environ.get("FIXTURES_DIR", os.path.join(CACHE, "fixtures"))
    src = subprocess.run([os.path.join(ROOT, "tools/games/mkimage.sh"), game, os.path.join(fix, g["fixture"]),
                          g["dir"]], check=True, stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
    img = os.path.join(vm, "game.img")
    subprocess.run(["cp", "--sparse=always", src, img], check=True)
    return "hdd_02_parameters = 63, 16, 406, 0, ide\nhdd_02_fn = %s\nhdd_02_ide_channel = 0:1\n" % img


def config(L, card, vm, serial, cimg, bootimg, extra=""):
    class A:
        voodoo, voodoo_recompiler, voodoo_threads, g100_mb, sound = 0, 0, 1, 8, ""
    A.card = card
    path = L.build_config(vm, A, serial, cimg, bootimg, extra)
    text = open(path).read()
    text = text.replace("unittester_enabled = 1", "unittester_enabled = 0")
    text += ("\n[Network]\nnet_01_card = ne2k\nnet_01_net_type = slirp\n"
             "\n[NE2000 Compatible #1]\nbase = 0300\nirq = 10\n")
    open(path, "w").write(text)
    return path


def start(card, game=None):
    L = loopa()
    sd = state_dir(card)
    vm = os.path.join(sd, "vm")
    shutil.rmtree(vm, ignore_errors=True)
    os.makedirs(vm)
    control = os.path.join(sd, "control")
    if os.path.exists(control):
        os.remove(control)
    serial = os.path.join(sd, "serial.log")
    open(serial, "w").close()
    tmp = tempfile.mkdtemp(prefix="vpc-")
    try:
        bootimg, cimg = provision(L, card, vm, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    cfg = config(L, card, vm, serial, cimg, bootimg, game_disk(game, vm) if game else "")
    nvr = os.path.join(CACHE, "loopa", "nvr-bf6")
    if os.path.isdir(nvr):
        shutil.copytree(nvr, os.path.join(vm, "nvr"), dirs_exist_ok=True)
    errf = open(os.path.join(sd, "stderr.log"), "wb")
    xvfb, display = L.start_xvfb(errf)
    box = os.path.join(L.BOX86_DIR, "bin", "86Box")
    cmd = [box, "-P", vm, "-C", cfg, "-R", os.path.join(L.BOX86_DIR, "roms"), "-N",
           "-L", os.path.join(sd, "86box.log")]
    env = dict(os.environ, BOX86_MONITOR="1", HOME=vm, XDG_CONFIG_HOME=vm, SDL_AUDIODRIVER="dummy",
               DISPLAY=display)
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=errf, stderr=errf, env=env)
    print("vpc: %s running (pid %d); serial %s" % (card, p.pid, serial), flush=True)

    def console(c):
        try:
            p.stdin.write((c + "\n").encode())
            p.stdin.flush()
        except OSError:
            pass
    t0, taps = time.time(), 0
    try:
        while p.poll() is None:
            time.sleep(0.5)
            if os.path.getsize(serial) == 0 and time.time() - t0 > 6 + 3 * taps and taps < 12:
                console("key tap 0x3b")         # fresh NVRAM: "press F1 to continue"
                taps += 1
            if os.path.exists(control):
                what = open(control).read().strip()
                os.remove(control)
                print("vpc: %s" % what, flush=True)
                if what == "reset":
                    console("hardreset")
                elif what == "screenshot":
                    console("screenshot")       # into out/vbench/<card>/vm/screenshots/
                elif what == "stop":
                    console("exit")
                    break
        for _ in range(40):
            if p.poll() is not None:
                break
            time.sleep(0.25)
    finally:
        if p.poll() is None:
            os.kill(p.pid, signal.SIGKILL)
            p.wait()
        xvfb.terminate()
        errf.close()
    print("vpc: %s stopped" % card)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["start", "reset", "screenshot", "stop"])
    ap.add_argument("card", choices=["g100", "g200", "g400", "g450"])
    ap.add_argument("--game", help="attach this game's Loop A image as D: (bench.toml: games = 'D:')")
    a = ap.parse_args()
    if a.action == "start":
        return start(a.card, a.game)
    os.makedirs(state_dir(a.card), exist_ok=True)
    open(os.path.join(state_dir(a.card), "control"), "w").write(a.action + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
