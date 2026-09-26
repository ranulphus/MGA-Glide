# DOS-GL handoff (PRD Appendix B)

What MGA-Glide delivers to DOS-GL, item by item against PRD Appendix B,
with the evidence behind each answer. "Emulated" means 86Box with the local
patch series; "silicon" results wait for the bench (PRD milestone S) and
are marked **pending**.

## Delivered

| Item | Status | Evidence |
|---|---|---|
| **B1** G100, G200, G450 in the HAL | G100: complete (emulated). G200: complete against the emulated G200. G450: capabilities only | `hal/src/chip.c`; conformance 24/24 on both emulated cards |
| **B2** G100 blending | Stipple only: `ALPHACTRL` must hold `0x54` in its low byte, `astipple` (bit 11) turns the stipple on, `alphasel` picks texture, diffuse or modulated alpha. Additive and destination-reading blends cannot be expressed | `docs/combine-coverage.md`; G100 approximations |
| **B3** 86Box | The G100 needs `-DDEV_BRANCH=ON`. Use `tools/86box/build.sh` (pinned commit plus `tools/86box/series`) | local patches 0001–0005, 0101–0102 |
| **B4** G100 `ALPHACTRL` | Stipple alpha only, confirmed in 86Box. On silicon: **pending** | as B2 |
| **B5** Emulated G200 | `--card g200`: native `ALPHACTRL` blending and alpha test (also in plain `TRAP`), `TEXCTL2` specular and decal blend, `TEXORG1..4` mip levels, `DSTORG` with absolute `ZORG` | `docs/emulated-g200.md` lists what is modelled and what is guessed |
| **B6** HAL and harness | `tools/hal-export.sh` produces a standalone tree (HAL, reference rasteriser, smoke/probe/romdump, Loop A, 86Box build, patches). `make hal-djgpp smoke-djgpp` builds; the DJGPP smoke test passes in Loop A on both emulated cards (with CWSDPMI r7, fetched and pinned by `setup-djgpp`) | `tests/hal/smoke.c` |
| **B9** DPMI and crash handling | `hal/port/djgpp.c` implements the port API for DJGPP. The fault/exit hooks exist only in the DOS/4GW port (`sys_hook_faults` returns -1 elsewhere), so DOS-GL keeps its own top-level handler, as planned | `hal/include/mga/sys.h` |
| **B10** Toolchain neutrality | The HAL builds warning-free with Open Watcom, DJGPP (gcc 12.2, `-Werror`) and the host compiler; CI builds all three | `.github/workflows/ci.yml` |
| **B12** Shared test infrastructure | `tools/imgcmp.py` (565 quantisation, tolerance, edge masks, box filter, ignore rectangles, per-cell gating), `tools/loopa/run.py` (statuses, serial parsing, screenshots, `--card`), `VMODE` for display-state checks | `docs/loops.md` |

## Lessons DOS-GL should inherit

- **Initialise `ALPHACTRL` on blending chips.** Its reset value multiplies
  source and destination by zero, so every 3D trapezoid draws black.
  `engine_init` now writes ONE/ZERO; a program that bypasses it must too.
- **G200 origins.** `DSTORG` is in bytes and moves only the colour buffer;
  `ZORG` is absolute. On the G100, `ZORG` is relative to `YDSTORG` and the
  clip registers include `YDSTORG`.
- **Engine sync.** Wait for `FIFOSTATUS.bempty` and then `!STATUS.dwgengsts`;
  the busy bit alone races with the FIFO.
- **DOS time.** The PIT is in mode 3 (not monotonic when read directly);
  the BIOS tick count is reliable.
- **Emulator gaps affect references.** 86Box's Voodoo ignores the LFB
  Y-origin and lane swizzle, and its texture cache keys palettes by XOR
  (collisions). 86Box's MGA left TW15 texel alpha unset and shifted TW12
  fields (fixed locally: patch 0005). See `docs/reference-gaps.md`.
- **16-bit display goes through the palette.** The DAC indexes the palette
  with each component in the top bits (local patch 0003 makes 86Box do the
  same), so load a ramp at mode set even without gamma.

## Pending (needs the bench or cuda6)

| Item | What is missing |
|---|---|
| **B7** Throughput | Only emulator figures exist (`t23`: ~52 K small triangles/s in 86Box, which measures the emulator). Pentium II figures and the WARP decision come from milestone S |
| **B8** BAR layout, revision IDs, VBE lists, FIFO depth | Emulated values: framebuffer, MMIO and ILOAD apertures as in `tests/hal/probe.c` output; FIFO depth 64 on G100/G200 (documented 16 on G450). The G100 BIOS's VBE list is in `docs/loops.md`'s probe run. Physical cards: **pending** |
| **B11** cuda6 | `tools/rig/rig-probe.sh` reads the G200eR2's identity and BARs. The MMIO stages (register reset values, one `TRAP`, one `TEXTURE_TRAP`) are approved but not yet run |
| **B2/B4 on silicon** | Stipple pattern, alpha thresholds and texel-key behaviour under bilinear filtering (see the milestone S list in the plan) |
