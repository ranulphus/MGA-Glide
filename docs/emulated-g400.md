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
Patch `0009-mga-g400-dual-texture.patch` adds the second texture map and
the combiner (below). Neither models CRTC2, the MAVEN or the WARP setup
engine; the runtimes do not use them.

## Dual texturing (patch 0009)

From the G400 specification and the drivers that ran on real G400s,
collected in `docs/g400-dual-texture.md`. MGA-Glide never uses it; DOS-GL's
`GL_ARB_multitexture` does.

| Area | Model | Basis |
|---|---|---|
| Map 1's registers | A second copy of TMR0-8, TEXORG and TEXORG1-4, TEXWIDTH, TEXHEIGHT, TEXCTL, TEXCTL2, TEXTRANS(HIGH), TEXFILTER, TEXBORDERCOL, ALPHACTRL | specification (the per-map table, p.3-216) |
| Routing | `tmap0dis` is the OR of bit 31 last written to TEXCTL2, TEXWIDTH and TEXHEIGHT and applies from the next write: writes reach both maps while it is 0, map 1 only while it is 1; a register's start alias (+0x100) starts the draw after the routing | specification; **guessed:** that the write setting the bit is itself broadcast (X.org's EXA code relies on it) |
| Sampling | With `TEXCTL2.dualtex`, map 1's coordinates step like map 0's (per pixel, per line, and with the left edge) and it is sampled with its own registers | specification |
| Combiner | TDUALSTAGE0 always (single texturing too), TDUALSTAGE1 with dualtex, after the legacy module (`TEXCTL.tmodulate`, decal blend, specular); zero words pass the texel through, so everything drawn before is unchanged. Products `(a*b)>>8` as the legacy modulate; the blend mode's two passes as the specification's datapath | specification (fields and datapath); **guessed:** rounding, the legacy module's place before the combiner, add2x/addbias order |
| Alpha | ALPHACTRL alphasel "from texture" takes the combiner's alpha | Mesa programs it this way |
| Blending in dual mode | Map 0's ALPHACTRL | **differs:** the silicon takes map 1's; runtimes write both the same |
| Not modelled | TEXBORDERCOL (stored), bump mapping, table fog, Rev A's zero-TDUALSTAGE rule, TEXCTL2 bit 15 | |


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
| Pixel clock | Fvco = Fref·(N+1)/(M+1), Fo = Fvco/(P+1) with **Fref = 14.31818 MHz** | Fvco = 2·27 MHz·(N+2)/(M+1), Fo = Fvco/2^((P&3)+1) | G400: the formula is the specification's, but 897-21's tables only make sense with a 14.318 MHz reference (640x480: M=1, N=27, P=7 → 25.06 MHz; at the specification's 27 MHz the VCO would be 378 MHz, beyond its 310 MHz limit). G450: as the Linux matroxfb driver models it; 935-20 then programs 25.07 MHz. Both inferred: check on the cards |

Under both BIOSes PROBE reports VBE 3.0, the real LFB address, the
expected VRAM size, working engine and depth fills, and the same DAC and
CRTC state as the G200 after setting 640x480x16. Conformance is 26/26 on
both cards, and the GTA and Screamer Rally trace replays pass on both.

## Still unchecked

Everything specific to the physical cards (Loop B): the OPTION values the
BIOSes leave (`50040120` on the G400, `40091120` on the G450 in 86Box),
the pixel-PLL reference and formula above, the real G450's clip quirk (`g400_clip_quirk` in the HAL
caps is not yet used), and the G450 PCI card's PLX bridge (the HAL now
scans every PCI bus, so a card behind a bridge is found).
