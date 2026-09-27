# The emulated G400 and G450 (86Box local patch 0008)

`tools/86box/patches/local/0008-mga-g400-g450.patch` adds two AGP devices,
"Matrox Millennium G400 (MGA-Glide emulation)" (`millennium_g400`) and
"Matrox Millennium G450 (MGA-Glide emulation)" (`millennium_g450`), so the
runtime's G400-family path can be exercised before the physical cards
(PRD M6). Like every 86Box change here it is local and never upstreamed
(PRD D4). Select them with `--card g400` / `--card g450` (or
`MGA_CARD=...`) in `tools/loopa/run.py`, `tools/conform/run.py` and
`tools/games/replay.py`.

Both build on the emulated G200 (`docs/emulated-g200.md`): the drawing
engine, blending, alpha test, specular and mipmaps are the G200 model's.
Neither models the G400's second texture unit, its new combiner
(`TDUALSTAGE0/1`), CRTC2 or the MAVEN; the runtime does not use them.

## What differs from the G200 model

| Area | G400 | G450 | Source |
|---|---|---|---|
| PCI identity | `102B:0525`, revision 3 | `102B:0525`, revision `0x82` | Matrox PCI IDs; the G450 is a G400 with revision `0x80` and up, and the G400 BIOS applies extra fixes only to revision 2 (`897-21.asm`) |
| Video BIOS | Matrox G400 BIOS 897-21 | Matrox G450 BIOS 935-20 (the 34 KB image; the flasher module appended to the file is ignored) | Matrox's public `setup257.exe`, unpacked by `tools/86box/build.sh` into the local ROM set, never committed |
| Memory | 16 or 32 MB (`memory = 16 \| 32`) | same | card configurations |
| Framebuffer BAR | 32 MB | 32 MB | G400 specification |
| Bus FIFO | 16 entries (`FIFOSTATUS.fifocount<4:0>`, reset `0x210`) | same | G400 specification, FIFOSTATUS |
| CRTC offset | always 128-bit units, start address 64-bit units (the G200 model gets this only with `OPTION.interleave`, which means something else on the G400) | same | G400 specification §4.6.5 |
| `CRTCEXT6..8` | stored and read back (the G400 BIOS reads `CRTCEXT8`) | same | G400 specification |

Under both BIOSes PROBE reports VBE 3.0, the real LFB address, the
expected VRAM size, working engine and depth fills, and the same DAC and
CRTC state as the G200 after setting 640x480x16. Conformance is 25/25 on
both cards, and the GTA and Screamer Rally trace replays pass on both.

## Still unchecked

Everything specific to the physical cards (Loop B): the OPTION values the
BIOSes leave (`50040120` on the G400, `40091120` on the G450 in 86Box),
PLL behaviour, the real G450's clip quirk (`g400_clip_quirk` in the HAL
caps is not yet used), and the G450 PCI card's PLX bridge (the HAL now
scans every PCI bus, so a card behind a bridge is found).
