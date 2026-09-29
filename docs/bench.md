# Loop B: the physical bench

Real-silicon loop (PRD §11.7, D7). Nothing here needs a person except
provisioning and resetting a machine after a hard hang when it has no
reset hook.

## How a job runs

1. `tools/bench/run.py` (on the build host, not in the dev container)
   writes the job to `dist/bench/<pc>/job/`, a `FETCH.BAT` and a new
   `LATEST.TXT`. It serves `dist/bench` over HTTP unless
   `tools/bench/serve.sh` already does, and starts an upload-only FTP
   receiver (`tools/bench/ftpsink.py`) on the PC's `ftp_port`.
2. The PC's poller (`C:\HX\BENCH.BAT`) sees `LATEST.TXT` change, fetches
   the job into `C:\TEST` with mTCP `HTGET` (three tries per file) and
   runs `C:\TEST\RUN.BAT`. The job carries its DPMI host: `CWSDPMI.EXE` for
   DJGPP programs (detected from the go32 stub; DOS-GL), `DOS4GW.EXE`
   otherwise (`--extender` overrides).
3. The program reports over COM1 (`HX-` lines, and `MGL-` lines from the
   runtime). A snapshot with no 86Box unit tester prints
   `HX-CAPTURE <name>` and holds the frame for `HX_CAPWAIT` seconds; the
   host grabs it from the PC's capture device.
4. `RUN.BAT` uploads `C:\OUT` with mTCP `FTP` (a generated script of `put`
   lines; the password is a per-job token), then the poller reboots the PC.
   `REBOOT.COM` resets through the PCI reset control register (port
   `CF9h`, a full chipset reset) and falls back to the keyboard controller;
   a keyboard-controller reset alone resets only the CPU and in 86Box
   sometimes left the machine executing garbage at the reset vector.
5. Results land in `out/bench/<pc>/<job>/`: `serial.log`, `files/` (the
   PC's `C:\OUT`), PNGs converted from its PPMs, `capture-*.png`,
   `result.json`. On `HANG` or `TIMEOUT` the PC's `reset` hook runs.

Statuses: `PASS FAIL CRASH HANG TIMEOUT NOT-PICKED-UP`.

## Machines

`tools/bench/bench.toml` lists the PCs; machine-specific values go in
`tools/bench/bench.local.toml` (not committed, same layout, overrides per
key). Each PC has a card, a serial device on the build host, an FTP port,
an optional V4L2 capture device (and ffmpeg input options), an optional
reset command, and the directory its games are installed in.

| PC | Card | Serial port (default) | Notes |
|---|---|---|---|
| bench-g100 | Productiva G100 | /dev/ttyS0 | milestone S |
| bench-g200 | G200 | /dev/ttyUSB0 | M5 |
| bench-g400 | G400 AGP 16 MB | /dev/ttyUSB1 | |
| bench-g450 | G450 PCI 32 MB | /dev/ttyUSB2 | M6; DVI output for the capture device |
| vbench-g200, vbench-g100 | 86Box | `file:out/vbench/<card>/serial.log` | the dry run below |

The build host has one real UART (`ttyS0`); the others need USB serial
adapters. The user running `run.py` needs to be in the `dialout` group
(serial) and the `video` group (capture).

## Provisioning checklist (per PC)

1. MS-DOS 7.1 (FAT32) or 6.22 on C:, `HIMEM.SYS` only (no EMM386).
2. NIC with a DOS packet driver in `C:\PKTDRV\`; mTCP (`DHCP`, `HTGET`,
   `FTP`, pinned in `tools/setup/versions.mk`) in `C:\MTCP\` with
   `TCP.CFG` (`PACKETINT 0x60`) and `SET MTCPCFG=C:\MTCP\TCP.CFG`.
3. Null-modem cable from COM1 to the build host's serial port above.
4. Video capture: the card's output into the capture device (the Epiphan
   DVI2USB 3.0 is a UVC device: `capture = "/dev/videoN"`). Check it with
   `tools/bench/capture.py list`, `formats /dev/videoN` and
   `grab /dev/videoN x.png`, which reports the mode it saw (720x400 for
   DOS text). Epiphan's own tool may be needed once to switch the unit's
   firmware to UVC mode.
5. Recommended: a USB relay or smart plug on reset or power, with the
   command that pulses it as `reset`.
6. Copy `tools/bench/dos/BENCH.BAT` and `build/ow/dos/*.COM` to `C:\HX\`.
   At the end of `AUTOEXEC.BAT`: load the packet driver,
   `SET HX_URL=http://<host>:8000/<pc>` and `CALL C:\HX\BENCH.BAT`.
7. Install the games once: `C:\GAMES\GTA\` and `C:\GAMES\SR\` from the
   same archives as Loop A. Game jobs back up the retail `GLIDE2X.OVL` as
   `GLIDE2X.MGB` once, swap in the job's OVL and restore it afterwards.
8. Boot once; the harness should show `HX-BOOT` and `HX-IDLE` lines:
   `python3 tools/bench/serlog.py --port /dev/ttyS0`.

## Running

```
tools/bench/run.py --pc bench-g200 --exe build/ow/dos/PROBE.EXE
tools/bench/run.py --pc bench-g200 --exe build/ow/dos/CONFORM.EXE --args t04 \
    --ovl build/ow/GLIDE2X.OVL
tools/bench/run.py --pc bench-g200 --replay ~/.cache/mga-glide/traces/gta-voodoo-300.bin \
    --frames 60,150 --ovl build/ow/GLIDE2X.OVL
tools/bench/run.py --pc bench-g200 --game sr --ovl build/ow/GLIDE2X.OVL \
    --set MGAGLIDE=exit_after=400
tools/bench/run.py --pc bench-g450 --file A.EXE --file B.EXE --ovl build/ow/GLIDE2X.OVL \
    --cmd "A.EXE --x" --cmd "B.EXE --glide=C:\TEST\GLIDE2X.OVL"
```

`--cmd` runs several programs in one job, each line in turn in `C:\TEST`
(DOSBench runs its OpenGL and Glide programs back to back this way): every
EXE shipped with `--file` gets its DOS extender, and the job passes when every
program reports `HX-DONE 0`. Loop A's `run.py --cmd` likewise waits for every
program that reports `HX-START` to report `HX-DONE` (and, for DOS-GL programs
run with `DGL_EXIT_AFTER`, every `DGL-START` to be matched by a `DGL-EXIT`).

Game jobs need a game that runs unattended: Screamer Rally's attract-mode
race does; GTA waits at its menus for Enter, which only Loop A can press.
The runtime logs nothing per frame, so game jobs rely on `--timeout`
rather than the idle rule.

## Dry run in 86Box

`tools/bench/vpc.py` boots an emulated PC provisioned like a bench PC
(NE2000 on SLiRP with the Crynwr packet driver, mTCP, `BENCH.BAT`, COM1
to a log file, no unit tester) and leaves it polling:

```
MGA_DOCKER_NETWORK=host tools/dev python3 tools/bench/vpc.py start g200 [--game sr]
tools/bench/run.py --pc vbench-g200 --exe build/ow/dos/PROBE.EXE
tools/bench/vpc.py screenshot g200        # out/vbench/g200/vm/screenshots/
tools/bench/vpc.py stop g200
```

The container needs the host's network so SLiRP's `10.0.2.2` reaches
`run.py`'s HTTP and FTP ports. The dry run covers pick-up, fetch, the
`HX-CAPTURE` path, FTP upload, reboot, hang detection and the reset hook;
frames from it match Loop A pixel for pixel.
