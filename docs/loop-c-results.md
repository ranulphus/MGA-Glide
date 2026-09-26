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

## Stages (2026-09-26)

| Stage | Result |
|---|---|
| `regs` (read only) | `FIFOSTATUS = 0x240`: 64 free entries, `bempty` (bit 9) set, so the FIFO is 64 deep and the HAL's sync bit is right. `STATUS = 0x80020024`: engine idle. `ALPHACTRL` reads 0 at reset, which is why `engine_init` must set it on blending chips. Most drawing registers read back 0 |
| `sync` | `DWGSYNC` reads back the value written |
| `trap` | engine fill and one trapezoid triangle from the HAL's setup code: every sampled pixel as expected |
| `tex` | one `TEXTURE_TRAP` (8x8 TW16 texture, point sampled): texels as expected. **The G200eR2 kept its 3D texture engine**, so it is a usable G200-family reference for the emulated G200 |
