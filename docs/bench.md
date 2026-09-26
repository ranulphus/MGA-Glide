# Loop B: the physical bench

Real-silicon loop (PRD §11.7, D7). Nothing here needs a person except
provisioning and resetting a machine after a hard hang.

## Machines

| PC | Card | Serial port on build host | Notes |
|---|---|---|---|
| bench-g100 | Productiva G100 | /dev/ttyS0 | milestone S |
| bench-g200 | G200 | /dev/ttyS1 | M5 |
| bench-g450 | G450 PCI | /dev/ttyS2 | M6 |

Fill in the real hostnames, card models and PCI revisions when a PC is
provisioned. `HX-BOOT` lines from each PC report what it found.

## Provisioning checklist (per PC)

1. MS-DOS 7.1 (FAT32) or 6.22 on C:, `HIMEM.SYS` only (no EMM386).
2. NIC with a DOS packet driver in `C:\PKTDRV\`; mTCP in `C:\MTCP\` with
   `TCP.CFG` (`PACKETINT 0x60`) and `SET MTCPCFG=C:\MTCP\TCP.CFG`.
3. Null-modem cable from COM1 to the build host's serial port above.
4. Video capture: the card's output into a capture device on the build host
   (`/dev/videoN`), or a VGA/DVI-to-HDMI converter into one.
5. Optional but recommended: a USB relay or smart plug on reset or power,
   so a hang does not need a person.
6. Copy `tools/bench/dos/*` and `build/ow/dos/*.COM` to `C:\HX\`; add
   `CALL C:\HX\BENCH.BAT` to the end of `AUTOEXEC.BAT`.
7. Install the games once: `C:\GAMES\GTA\` and `C:\GAMES\SR\` from the
   same archives as Loop A. Runs replace only `GLIDE2X.OVL`.
8. Boot once; the harness should show `HX-BOOT` and `HX-IDLE` lines:
   `python3 tools/bench/serlog.py --port /dev/ttyS0`.

## Running

```
tools/bench/serve.sh                      # HTTP server for build outputs (port 8000)
tools/bench/run.py --pc bench-g100 --exe build/ow/dos/LESPIKE.EXE \
    --ovl build/ow/GLIDE2X.OVL --args "--draw"
```

`run.py` publishes a job (`dist/bench/<pc>/LATEST.TXT`), the PC's poller
fetches and runs it, `serlog.py` waits for `HX-DONE`, the capture device
supplies a frame, and results land in `out/bench/<pc>/<name>/`.
