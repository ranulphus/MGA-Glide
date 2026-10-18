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
line. An `@TEXT` item starts a new count from the moment TEXT appears on the
serial line (after the previous anchor's match): under load 86Box runs
slower than the wall clock, so a script that waits for the guest's own
lines ("the level is loaded", "saved") holds its order where fixed times
drift. `--mouse ps2` gives the machine a PS/2 mouse and loads CuteMouse
(FreeDOS 1.4's `ctmouse.zip`, fetched once) before the test; then
`SECONDS:mouse:DX:DY[:BUTTONS]` items move it by DX, DY mickeys and set its
buttons (bit 0 left, 1 right, 2 middle) through the monitor's `mouse`
command (local patch 0104, which also marks the mouse captured: 86Box's mice
report nothing otherwise). `make loopa-selftest` checks it with
`MOUSETST.EXE` (`tests/shim/mousetst.c`).

`--joystick TYPE` gives the machine a joystick (86Box's `joystick_type`,
e.g. `2axis_4button`; `4axis_4button` when `--keys` has joy items and no
type is given): a virtual one (local patch 0105, `BOX86_VJOY=1`) that
`SECONDS:joy:axis:N:VALUE` (-32767..32767) and `SECONDS:joy:button:N:0|1`
items set through the monitor's `joy` command. Axis N is the game port's
axis N (bit N of port 0x201; run.py maps the 4-axis types' rudder and
throttle straight), and 86Box adds a standalone game port when no sound
card brings one. `make loopa-selftest` checks it with `JOYTEST.EXE`
(`tests/shim/joytest.c`).

`--wav` records the sound card's output to `out/<name>/audio.wav`: 86Box
plays through OpenAL Soft, whose wave backend (`ALSOFT_DRIVERS=wave`, 16-bit)
writes the file. The wave backend keeps wall-clock time and 86Box does not,
so a recording has the guest's sounds with silences between them;
`tools/loopa/wavcheck.py FILE --tone HZ [--max-gap MS]` looks for tones in
the loud stretches. `make loopa-selftest` plays a 440 Hz tone with
`SBBEEP.EXE` (`tests/shim/sbbeep.c`, the SB DAC in direct mode) under
`--sound sb16`.

Two 16-bit helpers in `C:\HX` check that a program gave the machine back:
`KEYWAIT <seconds>` drops keys already in the BIOS buffer, prints
`HX-KEYWAIT ready`, then reports the next key read through INT 16h
(`HX-KEY scan=.. ascii=..`, or `HX-KEY none`), which shows IRQ 1 is the
BIOS's again; `VECCHK save|check` compares the real-mode vectors of IRQ 0,
1, 5, 7 and 12 and the PIC masks with a saved copy (`HX-VECCHK ok`, or one
`HX-VECCHK changed` line per difference).

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

### Machine options (for GLOS)

These options serve GLOS (`~/GLOS`), whose supervisor runs DOS in
virtual-8086 mode. None changes what an existing job generates:
`make loopa-cfgcheck` compares `run.py --emit-config` output (the 86box.cfg
with placeholder paths, RUN.BAT and the golden image key) for a set of cases
with `tools/loopa/ref/`. It needs no emulator; after a deliberate change,
`tools/loopa/cfgcheck.py --update` rewrites the references for review.

| Option | What it does |
|---|---|
| `--machine bf6\|486dx2\|486dx4` | `bf6` is the Pentium II 350 above. The 486 profiles are a Shuttle HOT-433A (UMC 8881, PCI, AwardBIOS 4.51PG) with an i486DX2-66 (no CR4, no CPUID) or an Intel iDX4-100 (CR4 with VME and PVI). Each profile has its own NVRAM cache. |
| `--card vbe` | An S3 Trio64V2/DX with its "Generic" BIOS (VBE 2.0, 4 MB, linear framebuffer). The default on the 486 profiles: 86Box's Matrox cards are AGP, so an AGP Matrox card there is a setup error. `VBEINFO` reports the VBE version, memory and modes. |
| `--card g200pci\|g400pci\|g450pci` | PCI versions of the G200, G400 and G450 (local patch 0116), for any profile. The G200 has Matrox's PCI BIOS (899-33) and the PCI device ID 0520h; the G400 and G450 have the same BIOSes and IDs as the AGP ones. Each has the memory of its AGP twin (`--g100-mb`, at least 16 MB on the G400 and G450). |
| `--boot-cfg himemx` | A boot floppy with `DEVICE=HIMEMX.EXE` and `DOS=HIGH` (`tools/loopa/mkgolden-variant.sh`; HIMEMX pinned from the FreeDOS 1.4 repository). The default floppy and its key are untouched. `XMSINFO` reports the XMS driver and whether DOS is in the HMA. |
| `--boot-cfg glosshell`, `glosshell-himemx` | GLOS as the DOS shell: `SHELL=C:\TEST\GLOS.EXE /SHELL /P=A:\AUTOEXEC.BAT /E:2048`, without and with HIMEMX (FreeDOS ignores a `SHELL=` line much longer than this; GLOS finds `A:\FREEDOS\BIN\COMMAND.COM` itself). The job supplies `GLOS.EXE` and `GLOSK.BIN` in `C:\TEST` (`--file`); GLOS runs AUTOEXEC.BAT and so RUN.BAT. Used by GLOS's own tests. |
| `--net CARD` | A network card on SLiRP (`ne2k` ISA at 300h IRQ 10, `ne2kpci`, `rtl8139c+`, `i82557`, `i82558`) with host TCP ports forwarded into the guest: `--net-fwd [HOST:]GUEST`, default guest port 22 on a free host port, recorded in `result.json`. `--net-dos` puts the Crynwr NE2000 packet driver and mTCP's DHCP and NC on C:. |
| `--com2` | COM2 on a pty (86Box's named-pipe device opens it directly) that run.py relays to a TCP port on 127.0.0.1 (`result.json` `com2_port`; the guest's output also goes to `com2.log`): the route for a gdb stub in the guest. |
| `--tcp-send ANCHOR\|TARGET\|TEXT` | Once ANCHOR is on the serial line, connects to TARGET (`com2` or `net:GUESTPORT`), sends TEXT and CR LF, and keeps the reply in `tcp-N.txt`. |
| `--ssh-steps FILE --ssh-key KEY` | SSH steps against the guest's forwarded port 22 (GLOS's agent; needs `--net`), run in order once 86Box starts: `wait TEXT` (on the serial line), `exec COMMAND` (output in `ssh-N.out`/`.err`), `expect rc N`, `expect out TEXT`, `expect err TEXT`, `put LOCAL REMOTE` and `get REMOTE LOCAL` (scp; LOCAL relative to the steps file and the output directory), `shot NAME` (`glos shot` into `NAME.png`). The first failure ends the list and fails the job; every step is in `ssh.log` and `result.json` `ssh_steps`. `--ssh-user` defaults to `glos`. |
| `--wrap PREFIX` | Puts PREFIX in front of the program's command line (`GLOS.EXE /RUN ...`). |
| `--dynarec 0` | Runs 86Box's interpreter instead of the dynamic recompiler. |

`make loopa-selftest-ext` (`tools/loopa/selftest_ext.py`; `CHECKS="486 vbe"`
picks some) checks them:

| Check | What passes |
|---|---|
| `486` | HELLO and STACKPG on both 486 profiles |
| `vbe` | VBE 2.0 with a linear 640x480x16 mode on the BF6 and the 486dx2 |
| `net` | The host's text reaching mTCP NC listening on the guest's port 22 through the forward |
| `com2` | `COM2ECHO` echoing a line from the bridge |
| `himemx` | XMS 3.0 with DOS high, then HELLO and STACKPG |
| `cpu` | V86TEST on all three profiles |
| `pci` | The HAL's PROBE on the 486dx2 with each PCI Matrox card |
| `rtc` | RTCSET on the BF6 and the 486dx2: a change of century through INT 1Ah 05h leaves RTC registers A and B as they were and the year as set |

**V86TEST** (`tests/cpu/`) is a CPU self-test for the paths a virtual-8086
monitor depends on.
- A 16-bit loader runs the real-mode cases: the BIOS's INT 15h 86h against the
  RTC, and the A20 methods.
- It then enters protected mode with paging and calls a 32-bit monitor (host
  `gcc -m32`, `V86PM.BIN`). The monitor checks, against silicon behaviour:
  - V86 entry and exit;
  - IOPL-sensitive instructions;
  - INT3, INTO and BOUND;
  - the I/O bitmap;
  - VME and PVI;
  - gate checks and ESP0;
  - the 16-bit-SS ESP leak;
  - bad IRET frames;
  - page-fault error codes;
  - global pages;
  - the RTC rate;
  - FPU error reporting;
  - IOPL changes under the dynarec;
  - a load past a segment's limit, from code the dynarec has compiled (case V);
  - single-stepping onto an instruction that faults (case W);
  - EFLAGS.ID after an IRETD, on CPUs with and without CPUID (case X);
  - DR0-DR3 execute and data breakpoints (case Y).

It found six places where 86Box differed, and GLOS's tests found more. Local patches fix them:
- **0106:** a VME-redirected INT pushes IF = VIF and IOPL = 3.
- **0107:** INT3 and INTO in V86 mode go through the IDT.
- **0108:** the redirection bitmap applies at IOPL 3.
- **0109:** POPFD never loads VIF or VIP.
- **0110:** a byte port's two bitmap bytes must be inside the TSS limit.
- **0111:** the emulator ignores SIGPIPE. Found by GLOS's SSH tests: when a client closed its connection while
  SLiRP was still writing to the host socket, the write raised SIGPIPE and its default action ended 86Box
  (status CRASH, exit -13). QEMU ignores it the same way.
- **0112:** the dynarec checks segment limits on loads, as it did on stores. Found by GLOS's M4b with DJGPP's
  djtst205: DJGPP turns Ctrl-C and its timer into signals by cutting DS's limit to 4 KB in the IRQ handler, and
  in a loop that only reads, the fault never came, under any DPMI host. Flat segments (DOS/4GW) compile as
  before; V86TEST case V covers it.
- **0113:** a faulting instruction takes no single-step trap. Found by GLOS's M4c: lDebugX stepping V86 code
  onto an ARPL (#UD in V86 mode, GLOS's breakpoint into its monitor) got a #DB in GLOS's ring-0 #UD handler.
  An exception raised during the instruction (#UD, #DE, #BR, #NM) left the TF trap pending, and it was taken
  at the handler's first instruction once the gate had cleared TF. DR6.BS was never set for a single step
  either (DR6 read 0). V86TEST case W.
- **0114:** IRETD loads only the EFLAGS[31:16] bits the CPU model has, as POPFD does. On the 486DX2 an IRETD
  with ID set (into V86 mode, to ring 0 or ring 3) kept ID, and the next trap frame showed it: GLOS builds the
  V86 FLAGS image from that frame, so lDebugX saw ID toggle and ran CPUID into #UD. An IRETD in V86 mode at
  IOPL 3 now loads RF, AC and ID. V86TEST case X.
- **0115:** 86Box is built with its debug-register support (the DEBUGREGS486 option), so DR0-DR3 execute and
  data breakpoints fire on every profile: none did before, and GLOS's DPMI watchpoints (0B00h) never
  triggered. While a data breakpoint is enabled, or an execute breakpoint is near the code being run, the
  dynarec interprets; with DR7 clear, compiled code is unchanged. The Pentium II's CPUID now reports DE, as
  silicon does. V86TEST case Y.

- **0116:** PCI versions of the G200, G400 and G450 (GLOS's D38: a 486 with a Matrox card). 86Box has the
  Matrox G-series as AGP cards only, though its Matrox code already handles a card without AGP (the
  Mystique). The new devices are the same cards on the PCI bus: the G200 reads device ID 0520h and boots
  Matrox's PCI BIOS, 899-33 from the same package as 900-33 (the AGP BIOS on a PCI G200 found 2 MB and no
  linear framebuffer); the G400 and G450 keep their BIOSes and device ID 0525h. Whether a real PCI G450's
  BIOS differs is a bench question (the user's G450 is a PCI board).
- **0118:** on the PIIX4, ports 74h-77h share 70h-73h's RTC index registers, as aliases do on Intel's south
  bridges (74h reads back the index 70h set). 86Box gave each pair its own index. Found by GLOS's M10 (setting
  the clock): the BF6's Award BIOS traps port 71h into SMM (PIIX4 device trap 12) and its handler works through
  74h/75h. After a change of century, the BIOS's own INT 1Ah 05h and DOS's DATE included, the handler moved
  only the alias's index, to the century register 32h, and from then on every read of port 71h returned the
  century. Registers A and B read 20h; reads of register C returned the century too, so the periodic
  interrupt's flag never cleared and a 1024 Hz RTC tick stopped. Selftest `rtc` covers it.

`tests/cpu/known-86box.txt` lists any remaining deviation the check accepts.
It is empty; the RTC firing with register C unread and PGE having no effect
are recorded as INFO.

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
