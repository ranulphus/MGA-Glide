# Test loops

| Loop | Where | What it answers |
|---|---|---|
| A | 86Box on the build host (G100 + Voodoo Graphics, `bf6` board) | does it work, and does it match the Voodoo? |
| B | the physical DOS PCs (G100, G200, G450), over serial and the network | does it work on silicon, and how fast? |
| C | cuda6's G200eR2 through sysfs (read-only probes and approved MMIO) | register behaviour of real G200-family silicon |

## Loop A

`tools/loopa/run.py` builds a boot floppy and a C: disk (FreeDOS), writes
`RUN.BAT`, starts the patched 86Box under a private Xvfb, and follows the
guest's serial output (`HX-` lines from test programs, `MGL-` lines from the
runtime). It ends the emulator through the unit-tester device or a
timeout, collects `C:\OUT`, converts images to PNG, and writes
`out/<name>/{result.json,status,serial.log,*.png}`.

The machine has 64 MB unless `--mem MB` says otherwise (Half-Life on
DOS-GL runs with 128).

`--keys` types into the guest through the SDL monitor console (local patch
0101): `SECONDS:SCANCODE[:down|up]`, seconds counted from the first serial
line. `--mouse ps2` gives the machine a PS/2 mouse and loads CuteMouse
(FreeDOS 1.4's `ctmouse.zip`, fetched once) before the test; then
`SECONDS:mouse:DX:DY[:BUTTONS]` items move it by DX, DY mickeys and set its
buttons (bit 0 left, 1 right, 2 middle) through the monitor's `mouse`
command (local patch 0104, which also marks the mouse captured: 86Box's mice
report nothing otherwise). `make loopa-selftest` checks it with
`MOUSETST.EXE` (`tests/shim/mousetst.c`).

Statuses: `PASS`, `FAIL`, `TIMEOUT` (wall clock), `HANG` (serial silent for
the idle time), `GUEST-EXC` (the program died and RUN.BAT carried on),
`CRASH`, `EMU-FATAL` (86Box reported a fatal error).

After every program RUN.BAT runs `VMODE`, which reports the video mode
(`HX-VMODE`), so a run shows whether the display was left in text mode.

Besides the Matrox work, the patch series fixes one CPU fault in the
emulator. Local patch `0103-cpu-restartable-interrupt-frame.patch` covers
interrupts taken at the same privilege level, as CWSDPMI's DPMI calls are:
86Box lowered ESP after each of the frame's three pushes, so a page fault
on the second or third push left ESP 4 or 8 bytes low when the INT
restarted. That happened when the frame straddled into a stack page the
program had not yet touched. DJGPP's `__dpmi_int` then popped the saved CS
into SS and died with a #GP. Whether a program hit it depended only on
where its stack happened to sit (DOS-GL's conformance tests t13 and t15
did). Real processors restore ESP. `STACKPG.EXE`
(`tests/shim/stackpg.c`, DJGPP) makes DPMI calls with ESP at every offset
around fresh page boundaries, and `make loopa-selftest` fails without the
patch.

### Conformance (`tools/conform/run.py`)

`ref` runs a test on the retail runtime and the emulated Voodoo and stores
its frames in `tests/conform/ref/voodoo/<test>/`; `check` runs it on
MGA-Glide and the emulated G100 and compares (`tools/imgcmp.py`: 565
quantisation, per-channel tolerance, fraction of pixels allowed beyond it,
edge masking). `tests/conform/manifest.json` holds per-test and per-frame
tolerances, box filtering for G100 stipple, `ignore` rectangles for
documented reference-card gaps, and per-cell gating for the combine grid.

### Games

`--game gta|sr` attaches the installed game (from `~/DOSGAMES`) as D:,
with keys and screenshots on a timetable. `SET MGAGLIDE=...` in `--pre`
sets runtime options (for example `exit_after`, `stats`, `census`,
`trace`). Call traces replayed with GLPLAY on both paths give the REPLAY
comparison (`tools/games/replay.py`, see `docs/trace.md`).

## Loop B

See `docs/bench.md` for the bench layout and provisioning checklist.

## Loop C

`tools/rig/` holds the cuda6 probes, which stay within the approved scope:
`/sys/bus/pci/devices/0000:0a:00.0/*`, `~/mga-rig/` and `gdb`.
