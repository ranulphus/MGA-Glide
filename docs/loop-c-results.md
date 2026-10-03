# Loop C results: the G200eR2 on cuda6

cuda6 (Dell PowerEdge, Ubuntu, kernel 4.4) has a Matrox G200eR2 (PCI
`102b:0534`, subsystem `1028:04cf`, revision 0) as its boot VGA, with no
kernel driver bound. MGA-Glide reaches it through its sysfs resource files
only (the approved scope, PRD §4.4): `tests/rig/mgarig.c` on the HAL's
Linux port (`hal/port/linux.c`), built static on the build host and copied
to `~/mga-rig/`. All drawing goes to VRAM from 4 MB up, far from the text
console; nothing appears on screen.

| BAR | Address | Size | Use |
|---|---|---|---|
| 0 | `d2000000` | 16 MB | framebuffer (prefetchable) |
| 1 | `dcffc000` | 16 KB | control registers (MMIO) |
| 2 | `dc000000` | 8 MB | ILOAD aperture |

`mgarig` finds these itself: the Linux port answers PCI configuration
reads (mechanism #1) from the sysfs `config` file of `RIG_BDF` alone, so
the HAL's `mga_find()` reads the identity and BARs as it does under DOS.
Any other Matrox card under Linux works with `RIG_BDF=<domain:bus:dev.fn>`.

## Stages (2026-09-26)

| Stage | Result |
|---|---|
| `regs` (read only) | `FIFOSTATUS = 0x240`: 64 free entries, `bempty` (bit 9) set, so the FIFO is 64 deep and the HAL's sync bit is right. `STATUS = 0x80020024`: engine idle. `ALPHACTRL` reads 0 at reset, which is why `engine_init` must set it on blending chips. Most drawing registers read back 0 |
| `sync` | `DWGSYNC` reads back the value written |
| `trap` | engine fill and one trapezoid triangle from the HAL's setup code: every sampled pixel as expected |
| `tex` | one `TEXTURE_TRAP` (8x8 TW16 texture, point sampled): texels as expected. **The G200eR2 kept its 3D texture engine**, so it is a usable G200-family reference for the emulated G200 |

## Experiments (`mgarig exp`, 32-bit target)

What the emulated G200 had guessed, measured on the G200eR2:

| # | Question | Result on silicon | Emulator (86Box local patches) |
|---|---|---|---|
| E1 | TW12 field expansion | Replication: texel `0x8888` gives colour `0x888888`, and alpha test EQUAL `0x88` passes while `0x80` fails | 0005 already replicates |
| E2 | TW15 alpha | The top bit reads as alpha 0 or 255 | 0005 already does this |
| E3 | Blend arithmetic (SRC_ALPHA / 1-SRC_ALPHA, source `0xC0` over `0x40`) | alpha 0, 1, 64, 128, 192, 254, 255 give `40 41 60 80 a0 bf c0`, the same in `TRAP` and `TEXTURE_TRAP` | `(s*a + d*(255-a) + 127) / 255` reproduces all seven; plain `TRAP` blends, as modelled |
| E4 | `ALPHACTRL.astipple` | Ignored: every pixel is drawn at any alpha | G200 only; the G100 keeps its stipple |
| E5 | Mip level choice (nearest-level mode, `fthres` 0x10) | Rounds lambda: ratios 1.33, 1.60, 2.67, 3.20, 5.33, 8 give levels 0, 1, 1, 2, 2, 3 | 0006 rounds (was floor) |
| E5b | `TEXFILTER.fthres` | No effect up to 0x20; larger values force level 0 below a threshold that is not a simple bias | not modelled |
| E6 | Colour key under bilinear magnification | The texel with the larger weight decides the key; keyed texels' colours still enter the filter (a pixel 56% into the opaque texel is drawn with 44% of the keyed texel's green) | 0006 (was: the top-left texel decides). MGA-Glide's colour bleeding into keyed texels makes the filtered edge match the Voodoo |

## devserver's G200eR2 (2026-10-02)

`retro@devserver` (`0000:09:00.0`, its console under `mgag200`) in a window
the owner agreed: `mgag200` unbound for each pass and bound again after,
four passes of about two minutes or less, no kernel errors. The same
`mgarig` stages, then DOS-GL's rig build (`make rig`) before and after the
triangle-path work (`docs/setup-perf.md`; DOS-GL `e55b112` and `1f87684`).

| Stage | Result |
|---|---|
| `regs` | `FIFOSTATUS = 0x240`, `STATUS = 0x00020020` (idle) |
| `sync`, `trap`, `tex` | as on cuda6 |
| `fifo` | FIFOSTATUS stays at 64 free through 62 writes while a 1024x1024 fill keeps the engine busy |
| `fifodeep` | the same through 512 writes (the fill alone takes 5.4 ms; the writes 1.2 ms): this chip's FIFOSTATUS does not count queued writes, so per-write FIFO accounting can only be checked on retail cards |
| `cost` | a FIFOSTATUS read 2.06 us; a register write 212 ns; `fifo_need(1)` plus a write 245 ns (PCIe to the iDRAC's G200eR2, from a Xeon E5-2697 v4) |
| `overlap`, `bigtri` | flat triangles taller than 768 rows draw incompletely, with or without an idle wait before the next draw (below) |

**Register shadows on silicon.** DOS-GL's 27 conformance tests and 34 timed
scenes (`rigperf`: flat, Gouraud, textured and blended batches of 16-1024
pixel triangles, a perspective textured fogged mesh, a Gouraud mesh) give
byte-identical frames with the library before and after the work, which
writes every register every triangle and skips unchanged ones
respectively.

**Time per triangle (rigperf, medians of 7, two runs each):** where the bus
limits, the new HAL is faster: 16-256 px flat 4.78 -> 2.77 us, Gouraud
4.79 -> 4.27, textured 7.32 -> 3.73, blended 7.91 -> 3.98 (identical
triangles repeated: the shadows' best case). 1024 px triangles, which the
engine limits, and the meshes (textured mesh 31.9 -> 30.4 us) change
little. The Xeon's CPU time is small here; on a Pentium II the CPU saving
(about 30%, `docs/setup-perf.md`) adds to this.

**Edges overflow AR0-AR6.** Every Matrox specification from the Millennium
to the G200 gives AR0, AR2, AR4, AR5 and AR6 18 bits, signed (AR1, AR3: 24);
the G400's gives them 22. The trapezoid walk (`while (AR1 < 0) { AR1 +=
AR0; x += dir } AR1 += AR2`, the same with AR4, AR6, AR5) keeps its error
terms in them, so values wrap. The setup wrote exact edges as
`AR0/AR6 = 256 * dY`, `AR2/AR5 = -256 * |dX|` (1/16 pixel twice) and
Voodoo edges as `AR0/AR6 = 65536`, `AR2/AR5 = -|slope|` in 16.16: on the
G100/G200 exact edges over about 512 pixels and Voodoo edges flatter than
2 pixels a row drew wrong (on the G400, Voodoo edges flatter than about 32).
`bigtri` showed it first (right triangles 1020 rows tall stop early), and
DOS-GL's conformance frames on this chip (t04, t07, t09, t15, t17, t18,
t20, t27: big background and floor triangles). 86Box and
`tests/unit/refrast.c` kept 32 bits, so Loop A never showed it.

`mgarig edgecal` (2026-10-03, `tests/rig/edgecal.h`: 17 triangles, tall,
wide, sub-pixel, off-screen, Voodoo slopes 1.5 to 64) draws each with the
old values and with the setup's, and `build/edgecmp g200e DIR` compares
them with refrast:

- The model: refrast with 18-bit fields whose sums wrap, and **crossed
  spans (right edge before left) drawing nothing**, reproduces the old
  values' frames pixel for pixel, 17 of 17, including every Voodoo slope.
  (86Box had drawn crossed spans backwards; with the walk right they occur
  only from overflowed edges.)
- The fix (`hal/src/setup/trap.c`): an edge that does not fit has a factor
  of 2 common to S and D divided out, with the remainder floored, which
  leaves every row's column the same (exact edges fit up to 8191 pixels);
  a Voodoo edge that still does not fit takes the exact centre rule; a
  triangle over 8191 pixels is drawn as four. Its frames match refrast
  pixel for pixel, 17 of 17. A triangle whose edges fit as they are (the
  usual case) costs one 32-bit check of its extent: GLQuake's library time
  went from 6,479 to 6,560 emulated cycles a triangle (+1.25%, DOS-GL PROF
  build, G450, two runs each).
- In Loop A with patch 0012, exact edges draw the ideal (32-bit) frames:
  DOS-GL's conformance and Quake on the G200 match frames from before the
  fix made on the old 86Box. Voodoo edges drawn with the centre rule differ
  from the Voodoo by a pixel here and there: GTA one pixel a frame on the
  G200, Screamer Rally 17-44 on the G200 and one on the G400/G450 (edges
  flatter than 32 pixels a row near the horizon); every replay passes its
  Voodoo comparison with the same fractions as before.

86Box local patch 0012 and refrast now model both (refrast also clears AR
and SGN on DWGCTL's arzero and sgnzero, as 86Box does). `tlutfar` (a
TLUT load whose AR0, a source address in pixels, is past 2^17, as DOS-GL's
is) drew white both times: the probe's TW8 set-up is incomplete, so whether
blits honour AR0's 18 bits is still open.
