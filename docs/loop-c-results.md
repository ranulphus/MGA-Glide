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
