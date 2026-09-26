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

Statuses: `PASS`, `FAIL`, `TIMEOUT` (wall clock), `HANG` (serial silent for
the idle time), `GUEST-EXC` (the program died and RUN.BAT carried on),
`CRASH`, `EMU-FATAL` (86Box reported a fatal error).

After every program RUN.BAT runs `VMODE`, which reports the video mode
(`HX-VMODE`), so a run shows whether the display was left in text mode.

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
