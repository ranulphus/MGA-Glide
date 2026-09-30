#!/usr/bin/env python3
"""Package a ready-to-boot 86Box machine for manual use (on Windows, or on
Linux with any 86Box that has the local patches).

Runs in the dev container (mtools). The machine is Loop A's: an ABIT BF6 (440BX)
board with a Pentium II 350, 64 MB, the chosen Matrox card and a Voodoo
Graphics, booting FreeDOS 1.4 from Loop A's golden floppy, with Loop A's hard
disk. C:\\RUN.BAT (which the floppy's AUTOEXEC calls) sets PATH, the Sound
Blaster variables, and runs any --run lines. No unit tester and no serial
log: it is for a person at the keyboard.

  mkwinvm.py --name dosbench-g450 --card g450 \\
      --file build/dos/BENCHG.EXE=/DOSBENCH/BENCHG.EXE ... \\
      --run "CD \\DOSBENCH" --run "ECHO Type DOSBENCH for the menu" \\
      [--readme NOTES.txt] [--out dist/dosbench-g450-vm.zip]

--d-file SRC=/PATH and --d-dir SRC=/PATH (a whole directory tree) fill a
second hard disk, D: (d.img, --d-cylinders, default 1000: ~492 MB; at most
1023, the BIOS CHS limit), for what does not fit on C:. D:\\ goes on PATH, so
launchers can live in its root.

--game KEY (gta, sr: tools/games/games.json) installs that game from the
local fixtures (FIXTURES_DIR, as Loop A does) into C:\\GAMES\\<dir>, keeps
its own GLIDE2X.OVL as GLIDE2X.3DF, puts --mga-ovl in C:\\GAMES\\MGAGLIDE.OVL
and writes C:\\GAMES\\<KEY>.BAT: the game on MGA-Glide (on the Matrox card),
or with "3DFX" on 3dfx's runtime (on the Voodoo). A zip with games holds
retail software: it is for the owner's own machine only.

The zip holds <name>/86box.cfg, boot.img, c.img (sparse, 492 MB unpacked),
d.img if asked for, nvr/ (Loop A's saved BIOS settings, so the first boot
does not stop at a CMOS checksum error) and README.txt. Open it with
86Box.exe -P <folder>, or unzip it into the 86Box manager's system directory
(its "Use existing configuration" copies only 86box.cfg, not the images). DOS/4GW and CWSDPMI go into C:\\HX with the HX helpers. Never pass
retail Glide runtimes, games or BIOS images to --file for a kit that will
be shared.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "loopa"))
import run as loopa  # noqa: E402

CFG = """[General]
confirm_exit = 1
confirm_save = 1
confirm_reset = 1

[Machine]
machine = bf6
cpu_family = pentium2_deschutes
cpu_speed = 350000000
cpu_multi = 3.5
cpu_use_dynarec = 1
fpu_type = internal
mem_size = {mem_kb}
time_sync = local

[Video]
gfxcard = {gfxcard}
voodoo = {voodoo}

[{gfxname}]
memory = {vram}

[3DFX Voodoo Graphics]
type = voodoo
framebuffer_memory = 2
texture_memory = 2
render_threads = 2
recompiler = 1
bilinear = 1
dithersub = 1

[Input devices]
keyboard_type = keyboard_ps2
mouse_type = ps2

[Sound]
sndcard = sb16

[Storage controllers]
hdc = internal
fdc = internal

[Ports (COM & LPT)]
serial1_enabled = 1
serial2_enabled = 0
lpt1_enabled = 0

[Hard disks]
hdd_01_parameters = 63, 16, 1000, 0, ide
hdd_01_fn = c.img
hdd_01_ide_channel = 0:0

{hdd2}
[Floppy and CD-ROM drives]
fdd_01_type = 35_2hd
fdd_01_fn = boot.img
fdd_01_check_bpb = 0
fdd_02_type = none
"""

README = """{name}: an 86Box machine for MGA-Glide's patched 86Box
===========================================================

Machine: ABIT BF6 (440BX), Pentium II 350, {mem} MB, {cardname} ({vram} MB) plus a
Voodoo Graphics (2+2 MB), Sound Blaster 16 (A220 I5 D1 H5), PS/2 mouse.
It boots FreeDOS 1.4 from boot.img (keep it in the floppy drive); C: is
c.img{d_note}. C:\\RUN.BAT runs at boot, sets PATH (C:\\HX has DOS4GW.EXE and
CWSDPMI.EXE) and BLASTER, and loads CuteMouse (C:\\HX\\CTMOUSE.EXE, GPL) for
the mouse.

Open: unzip this folder into the 86Box manager's system directory (shown in
the manager's Preferences; by default %USERPROFILE%\\86Box VMs) and start
the manager: it lists every folder there that holds an 86box.cfg. Or run
86Box.exe -P <this folder>. The disk images must stay beside 86box.cfg:
the manager's "Use existing configuration" copies only the configuration
text, so a machine added that way boots with no disks ("DISK BOOT FAILURE").
To copy files in or out, attach c.img to another tool that reads FAT16
partitioned images, or add your own second disk in Settings > Storage.

{notes}"""

GAME_BAT = """@ECHO OFF
REM {key}.BAT [3DFX]: {title}
REM Default: MGA-Glide on the Matrox card. 3DFX: 3dfx's runtime on the Voodoo.
C:
CD \\GAMES\\{cwd}
IF "%1"=="3DFX" GOTO VOODOO
IF "%1"=="3dfx" GOTO VOODOO
COPY /Y C:\\GAMES\\MGAGLIDE.OVL {ovlname} > NUL
ECHO {title}: MGA-Glide on the Matrox card
GOTO RUN
:VOODOO
COPY /Y {ovlbase}.3DF {ovlname} > NUL
ECHO {title}: 3dfx's runtime on the Voodoo
:RUN
{run}
CD \\
"""


def install_game(d, key, games, fixtures):
    """Copy a game from the fixtures to C:\\GAMES and write its launcher."""
    g = games[key]
    src = os.path.join(fixtures, g["fixture"])
    d.mkdir("/GAMES")
    d.mkdir("/GAMES/" + g["dir"])
    subprocess.run(["mcopy", "-s", "-Q", "-o", "-D", "o"] + [os.path.join(src, f) for f in sorted(os.listdir(src))] +
                   ["%s:/GAMES/%s/" % (d.letter, g["dir"])], env=d.env, check=True,
                   stdout=subprocess.DEVNULL)
    ovl = g["ovl"].replace("\\", "/")                  # e.g. GTA/GTADOS/GLIDE2X.OVL
    base, name = os.path.dirname(ovl), os.path.basename(ovl)
    retail = next(os.path.join(src, os.path.relpath(os.path.join(r, f), src))
                  for r, _, fs in os.walk(src) for f in fs
                  if os.path.relpath(os.path.join(r, f), src).upper() == os.path.relpath(ovl, g["dir"]).upper())
    d.put(retail, "/GAMES/%s/%s.3DF" % (base, os.path.splitext(name)[0]))
    bat = GAME_BAT.format(key=key.upper(), title=g["title"], cwd=g["cwd"], ovlname=name.upper(),
                          ovlbase=os.path.splitext(name)[0].upper(), run="\r\n".join(g["run"]))
    return key.upper() + ".BAT", bat.replace("\n", "\r\n").replace("\r\r\n", "\r\n")


HX_TOOLS = ("UTEXIT.COM", "SERSAY.COM", "WAITSEC.COM", "REBOOT.COM", "VMODE.COM")


def make_disk(img, tmp, cyl, label):
    """An empty partitioned FAT16 hard-disk image (63 sectors, 16 heads), as
    tools/games/mkimage.sh makes Loop A's game disks; returns its Disk."""
    subprocess.run(["truncate", "-s", str(63 * 16 * cyl * 512), img], check=True)
    d = loopa.Disk(img, tmp, "d")
    subprocess.run(["mpartition", "-I", "d:"], env=d.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["mpartition", "-c", "-t", str(cyl), "-h", "16", "-s", "63", "-a", "d:"], env=d.env, check=True)
    subprocess.run(["mformat", "-t", str(cyl), "-h", "16", "-s", "63", "-v", label, "d:"], env=d.env, check=True)
    return d


def put_file(d, spec):
    """--file / --d-file SRC=/DOS/PATH, making the directories on the way."""
    src, dst = spec.split("=", 1)
    dst = dst.replace("\\", "/")
    parts = dst.strip("/").split("/")[:-1]
    for i in range(1, len(parts) + 1):
        d.mkdir("/" + "/".join(parts[:i]))
    d.put(src, dst)


def put_dir(d, spec):
    """--d-dir SRC=/DOS/PATH: the directory tree SRC's contents under PATH."""
    src, dst = spec.split("=", 1)
    dst = dst.replace("\\", "/").rstrip("/")
    parts = dst.strip("/").split("/")
    for i in range(1, len(parts) + 1):
        d.mkdir("/" + "/".join(parts[:i]))
    subprocess.run(["mcopy", "-s", "-Q", "-o", "-D", "o"] + [os.path.join(src, f) for f in sorted(os.listdir(src))] +
                   ["%s:%s/" % (d.letter, dst)], env=d.env, check=True, stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--card", choices=sorted(loopa.CARDS), default="g450")
    ap.add_argument("--no-voodoo", dest="voodoo", action="store_false")
    ap.add_argument("--file", action="append", default=[], help="SRC=/DOS/PATH")
    ap.add_argument("--run", action="append", default=[], help="C:\\RUN.BAT line")
    ap.add_argument("--readme", help="text appended to README.txt")
    ap.add_argument("--game", action="append", default=[], help="install a game from the fixtures (gta, sr)")
    ap.add_argument("--mga-ovl", default=os.path.join(ROOT, "build/ow/GLIDE2X.OVL"),
                    help="MGA-Glide runtime for the games (default this checkout's build)")
    ap.add_argument("--d-file", action="append", default=[], help="SRC=/DOS/PATH on a second disk, D:")
    ap.add_argument("--d-dir", action="append", default=[], help="SRC=/DOS/PATH: a directory tree on D:")
    ap.add_argument("--d-cylinders", type=int, default=1000, help="size of D: (63 x 16 x N sectors, N <= 1023)")
    ap.add_argument("--mem", type=int, default=64, help="the machine's RAM in MB (default 64)")
    ap.add_argument("--out")
    a = ap.parse_args()
    if not 1 <= a.d_cylinders <= 1023:
        ap.error("--d-cylinders must be 1..1023 (the BIOS CHS limit)")
    out = os.path.abspath(a.out or os.path.join(ROOT, "dist", a.name + "-vm.zip"))
    tmp = tempfile.mkdtemp(prefix="mkwinvm-")
    try:
        vm = os.path.join(tmp, a.name)
        os.makedirs(vm)
        gold = loopa.golden()
        shutil.copyfile(os.path.join(gold, "boot.img"), os.path.join(vm, "boot.img"))
        subprocess.run(["cp", "--sparse=always", os.path.join(gold, "c.img"), os.path.join(vm, "c.img")], check=True)
        nvr = os.path.join(loopa.CACHE, "loopa", "nvr-bf6")  # saved by Loop A's first passing run
        if os.path.isdir(nvr):
            shutil.copytree(nvr, os.path.join(vm, "nvr"))
        d = loopa.Disk(os.path.join(vm, "c.img"), tmp)
        for t in HX_TOOLS:
            d.put(os.path.join(ROOT, "build/ow/dos", t), "/HX/" + t)
        d.put(os.path.join(loopa.WATCOM, "binw/dos4gw.exe"), "/HX/DOS4GW.EXE")
        if os.path.exists(loopa.CWSDPMI):
            d.put(loopa.CWSDPMI, "/HX/CWSDPMI.EXE")
        d.put(loopa.ctmouse(tmp), "/HX/CTMOUSE.EXE")   # the PS/2 mouse in DOS programs
        for spec in a.file:
            put_file(d, spec)
        path = "C:\\HX;A:\\FREEDOS\\BIN"
        hdd2 = d_note = ""
        if a.d_file or a.d_dir:
            dd = make_disk(os.path.join(vm, "d.img"), tmp, a.d_cylinders, "DATA")
            for spec in a.d_dir:
                put_dir(dd, spec)
            for spec in a.d_file:
                put_file(dd, spec)
            hdd2 = ("hdd_02_parameters = 63, 16, %d, 0, ide\nhdd_02_fn = d.img\nhdd_02_ide_channel = 0:1\n"
                    % a.d_cylinders)
            d_note = " and D: is d.img"
            path += ";D:\\"
        if a.game:
            games = json.load(open(os.path.join(ROOT, "tools/games/games.json")))
            fixtures = os.environ.get("FIXTURES_DIR", os.path.join(loopa.CACHE, "fixtures"))
            d.mkdir("/GAMES")
            d.put(a.mga_ovl, "/GAMES/MGAGLIDE.OVL")
            for key in a.game:
                name, text = install_game(d, key, games, fixtures)
                bat = os.path.join(tmp, name)
                open(bat, "w", newline="").write(text)
                d.put(bat, "/GAMES/" + name)
                print("mkwinvm: installed %s (%s)" % (key, games[key]["title"]))
            path = "C:\\GAMES;" + path
        run = ["SET PATH=" + path, "SET BLASTER=A220 I5 D1 H5 T6", "CTMOUSE", "C:", "CD \\"] + a.run
        rb = os.path.join(tmp, "RUN.BAT")
        open(rb, "wb").write(loopa.dos_bat(run))
        d.put(rb, "/RUN.BAT")
        section = loopa.CARDS[a.card][1]
        vram = 16 if a.card in ("g400", "g450") else 8
        open(os.path.join(vm, "86box.cfg"), "w").write(
            CFG.format(gfxcard=loopa.CARDS[a.card][0], gfxname=section, vram=vram, voodoo=int(a.voodoo), hdd2=hdd2,
                       mem_kb=a.mem * 1024))
        notes = open(a.readme).read() if a.readme else ""
        if a.game:
            notes += ("\nGames (C:\\\\GAMES, on PATH; retail software: for your own machine only)\n"
                      "  %s\n"
                      "  Each runs on MGA-Glide on the Matrox card; add 3DFX (for example SR 3DFX) to run\n"
                      "  the game's own 3dfx runtime on the Voodoo instead. GTA waits for Enter at its\n"
                      "  menus. The launchers copy the chosen runtime over the game's GLIDE2X.OVL; 3dfx's\n"
                      "  original is kept beside it as GLIDE2X.3DF.\n" %
                      "\n  ".join("%-4s %s" % (k.upper(), json.load(open(os.path.join(ROOT, "tools/games/games.json")))[k]["title"])
                                  for k in a.game))
        open(os.path.join(vm, "README.txt"), "w", newline="\r\n").write(
            README.format(name=a.name, cardname=section, vram=vram, notes=notes, d_note=d_note, mem=a.mem))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if os.path.exists(out):
            os.remove(out)
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for dirpath, _, files in sorted(os.walk(vm)):
                for fn in sorted(files):
                    path = os.path.join(dirpath, fn)
                    z.write(path, os.path.join(a.name, os.path.relpath(path, vm)))
        print("mkwinvm: %s (%.1f MB)" % (out, os.path.getsize(out) / 1e6))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
