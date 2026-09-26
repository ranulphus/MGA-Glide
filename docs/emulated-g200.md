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
| Video BIOS | The Productiva G100 BIOS, with its PCI data structure and its three device-ID checks rewritten to `0x0521` at load. No G200 BIOS is in the ROM set; a dump from the bench G200 (WP-4) replaces this | Measured: without the checks rewritten the BIOS never programs the DAC depth |
| Origins | `DSTORG` moves the colour buffer (in bytes); `ZORG` is absolute | Matrox G200 register documentation, as used by the X.org and Mesa drivers |
| Blending | `ALPHACTRL` source and destination factors, in `TRAP` and `TEXTURE_TRAP`; destination alpha is 255 in 16 bpp | same |
| Alpha test | `ALPHACTRL.aten`, `atmode`, `atref` on the selected alpha | same |
| Specular | `TEXCTL2.specen` adds the iterated `SPECR/G/B` colour after texturing | same |
| Decal blend | `TEXCTL2.decalblend` blends texel and diffuse by texel alpha | same |
| Mipmaps | `TEXORG1..4`, `TEXFILTER.mapnb` and the `mm*` minification modes; the level comes from the per-pixel coordinate gradients | Level formula is the Voodoo's (`0.5 log2` of the larger squared gradient); the G200's own is unknown |
| `DWGSYNC` | reads back the last value written | same register documentation |

Patch `0005-mga-texel-alpha.patch` (both cards) gives TW15 texels their
1-bit alpha and expands TW12 fields by bit replication.

## To check on silicon

Each of these is a guess until the physical G200 (Loop B) and cuda6
(Loop C) confirm it; see the milestone S / M5 items in the plan:

- the mip level formula and the `fthres` threshold;
- 4-bit texel expansion (replication versus shift);
- blending precision and rounding;
- whether plain `TRAP` honours `ALPHACTRL` exactly as `TEXTURE_TRAP` does;
- `DSTORG` alignment rules.
