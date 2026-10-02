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

**Tall triangles overflow AR0-AR6.** The G200 specification gives AR0, AR2,
AR4, AR5 and AR6 18 bits, signed (AR1 24). Exact-mode edges write
`AR0/AR6 = 256 * dY` and `AR2/AR5 = -16 * 16 * |dX|` (1/16-pixel units
twice), and the engine's error terms wrap at 18 bits. `bigtri` (right
triangles (0,0), (w,0), (0,h)): every shape up to 768 rows tall draws
whole, any width; every shape 1020 rows tall does not (64x1020 stops after
about 16 rows). A model of the TRAP walk with 18-bit wrapping registers
(`while (AR4 < 0) { AR4 += AR6; x++ } AR4 += AR5` in 18 bits) gives
silicon's sample counts for 12 of the 14 shapes and breaks the same two
others; 19-bit or wider registers draw all 14 whole. 86Box and
`tests/unit/refrast.c` keep 32 bits, so Loop A never showed it. DOS-GL's
conformance frames on this chip show it (t04, t07, t09, t17, t18, t20, t27:
background and floor triangles that reach far outside the screen); the
same field widths are in the retail G200 specification.
