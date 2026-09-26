# The emulated G200 (86Box local patch 0004)

86Box emulates the Matrox G100 but not the G200. Patch
`tools/86box/patches/local/0004-mga-g200.patch` adds a "Matrox Millennium
G200 (MGA-Glide emulation)" AGP device (`millennium_g200`) so the G200 code
paths of the runtime can be developed and tested before the physical G200
and cuda6 checks (PRD WP-3, milestone M5). Like every 86Box change in this
project it is local and never upstreamed (PRD D4).

Select it with `--card g200` (or `MGA_CARD=g200`) in `tools/loopa/run.py`
and `tools/conform/run.py`.

## What it adds to the G100 model

| Area | Behaviour | Source |
|---|---|---|
| Identity | PCI device `0x0521`, family G200 | Matrox PCI IDs (public) |
| Video BIOS | Matrox's G200 AGP BIOS 900-33 (VBE 3.0), unpacked from Matrox's public `setup257.exe` into the local ROM set by `tools/86box/build.sh` (patch `0007`; never committed). Fallback, or with `MGAGLIDE_G200_BIOS=g100`: the Productiva G100 BIOS with its PCI data structure and three device-ID checks rewritten to `0x0521` at load | Matrox BIOS package (pinned by sha256 in `tools/setup/versions.mk`); the G100 fallback's rewrites were measured: without them it never programs the DAC depth |
| Origins | `DSTORG` moves the colour buffer (in bytes); `ZORG` is absolute | Matrox G200 register documentation, as used by the X.org and Mesa drivers |
| Blending | `ALPHACTRL` source and destination factors, in `TRAP` and `TEXTURE_TRAP`; destination alpha is 255 in 16 bpp | same |
| Alpha test | `ALPHACTRL.aten`, `atmode`, `atref` on the selected alpha | same |
| Specular | `TEXCTL2.specen` adds the iterated `SPECR/G/B` colour after texturing | same |
| Decal blend | `TEXCTL2.decalblend` blends texel and diffuse by texel alpha | same |
| Mipmaps | `TEXORG1..4`, `TEXFILTER.mapnb` and the `mm*` minification modes; the level comes from the per-pixel coordinate gradients and the nearest-level modes round it | Rounding measured on the cuda6 G200eR2 (`docs/loop-c-results.md`, E5) |
| `DWGSYNC` | reads back the last value written | same register documentation |

With the genuine BIOS, PROBE reports VBE 3.0 and the real LFB address in
every mode entry (the G100 BIOS reported `fffffff0`), `OPTION=4007dd21`
and subsystem ID 0 (the package image carries no board subsystem bytes at
`7FF8h`). The DAC state after the mode set (`HX-STAT dac`, `HX-STAT crtc`)
is identical under both BIOSes; the conformance suite and both game
replays pass unchanged.

Patch `0005-mga-texel-alpha.patch` (both cards) gives TW15 texels their
1-bit alpha and expands TW12 fields by bit replication; patch
`0006-mga-measured-texturing.patch` makes the larger-weight texel decide
colour keys under bilinear filtering and rounds the mip level. All three
behaviours were then confirmed on the G200eR2 (Loop C).

## Checked on the G200eR2 (Loop C)

Texel expansion, TW15 alpha, blend arithmetic, blending in plain `TRAP`,
mip level rounding and colour keys under bilinear filtering: see
`docs/loop-c-results.md`. Still unchecked: large `fthres` values, the
two-level (trilinear) modes, `DSTORG` alignment rules, and anything that
might differ between the G200eR2 and a retail G200 (Loop B).
