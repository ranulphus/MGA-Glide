# The G200's WARP setup engine

The G200 has a WARP setup engine, as the G400 does (`docs/g400-warp.md`). It has one WARP processor where the
G400 has two. On the G400 the WARP path made DOS-GL's DOSBench scenes three to five times faster on a
Pentium II 400. The G200 is the minimum card of DOS-GL's games: Fifth Wheel, and Echoes of the Void, whose PRD
(D32) asks for this path before its EV2. The owner decided on 2026-11-01 to bring it forward (PRD D2, amended).

`warp_g200` keeps the G200 path off until the real card has passed (G6 below). Tests turn it on: WARPTRI does, and
DOS-GL's `DGL_WARP=1`.

## Sources

| Source | What it gives |
|---|---|
| linux-firmware `matrox/g200_warp.fw` (MIT, LICENSES/LICENSE.mga; sha256 `6f97edeb…829c8d`) | the microcode: `hal/src/warp_g200.h`, made by `tools/warpfw.py` |
| Linux's mga DRM driver, v6.2 (`mga_warp.c`, `mga_state.c`, `mga_dma.c`; MIT) | the G200's init and per-pipe sequences, its texture registers, the AGP 2x PLL |
| Mesa 7.11's mga DRI driver (`mga_texstate.c`, `mgavb.c`, `t_dd_vbtmp.h`) | the vertex (as the G400's), tw = log2 + 28 |
| MGA-G200 specification, Nov 1998 (bitsavers) | the registers that exist, the instruction memory, the DMA channels, AGP |

The research, with citations, was done on 2026-11-01. Its conclusions are below.

## The G200 against the G400

| | G200 | G400 |
|---|---|---|
| WARPs | 1 (STATUS.wbusy <18> only) | 2, taking alternate triangles |
| Microcode | 8 pipes, 1,208-1,504 bytes: TGZ ... TGZSAF (F 1, A 2, S 4); **no two-texture pipes** | 16 pipes, 824-1,256 bytes |
| Binary compatibility | none: 2 of 248 distinct 64-bit words in common | |
| Instruction memory | one, 2 KB: WIMEMADDR, WIMEMDATA 2000h-207Fh (WMISC.wmaster 0 to write it) | two: WIMEMDATA and WIMEMDATA1 |
| Start, suspend | WIADDR (1DC0h); WIADDRNB (1E60h) without the FIFO | WIADDR2 (1DD8h); WIADDRNB2 |
| At start (the DRM's values) | WGETMSB 1606h, WVRTXSZ 7, WFLAG 0, WR24 0, WR25 100h, WR34 0, WR42 FFFFh, WR60 FFFFh; three DMAPADs; WIADDR = 0 \| START | WGETMSB 0E00h, WVRTXSZ 1807h, WACCEPTSEQ, WFLAG/WFLAG1 0, WR56 12800.0f, WR49/53/57/61 0 |
| Reserved on the G200 | WACCEPTSEQ (no sequencer: no strips or fans), WIADDR2, WFLAG1, WIMEMDATA1, the second WARP's NB registers | |
| Vertex | 8 dwords, Mesa's: x, y with (-0.5, -0.375), z 0..1, 1/w, BGRA, specular BGR + fog, u0, v0 | the same, or 10 with u1, v1 |
| Texture sizes | TEXWIDTH/TEXHEIGHT with tw = log2 + 28 (mod 64), rfw = 2 - log2; the same values in WR24/WR34 | tw = log2 + 11; WR54/WR62 with \| 40h |
| Texel convention | filters from texel corners: DOS-GL shifts u and v by half a texel when filtering bilinearly, as its CPU path does | samples at texel centres itself |
| DMA | as the G400's: a primary list, vertices by the secondary channel in vertex mode, PRIMEND extended with no-op padding | |
| AGP | sideband addressing required (AGP_CMD.sba_enable); 2x needs AGP_PLL (1E4Ch) bit 0; the G200-PCI has none | sideband optional |

DOS-GL and Glide select pipe 7 (TGZSAF) on both chips, as Mesa did, so the G200 loads it once, and again only
after a soft reset.

## In the HAL (`hal/src/warp.c`)

The G400's code carries over except where the table differs: `pipe_code` reads the chip's own microcode; the load
writes WIMEMDATA only; `g200_start` writes the G200's values and starts through WIADDR; `started` waits for wbusy;
`warp_tex` and `warp_tex_sizes` write the G200's encoding into WR24/WR34; `warp_reserve_seq` returns NULL (callers
send triangles); `agp_enable` refuses AGP without sideband and sets AGP_PLL for 2x. The soft reset before each load
(the G400's third-load failure) stays until the card shows whether the G200 needs it.

## In 86Box (local patch 0126)

Patch 0122's model, extended: the G200's pipes recognised by length and CRC-32; one WARP started through WIADDR;
the reserved G400 registers ignored; wbusy alone; lists only; the triangles set up with the HAL's G200 capabilities
(18-bit AR fields, one map). The G400's third-load failure is not modelled on the G200.

WARPTRI passes in Loop A on the emulated G200 (2026-11-01), by DMA and by the CPU: pipe 0 loads and reads back,
and Gouraud and depth triangles through the WARP match `setup_triangle` pixel for pixel. The G400's WARPTRI and
`seq` still pass with the patch.

## The plan

| Step | What | Where |
|---|---|---|
| G0 | The research above | done |
| G1 | `warp_g200.h` from linux-firmware; `tools/warpfw.py` (it reproduces `warp_g400.h`'s bytes too) | done |
| G2 | The HAL's G200 path; WARPTRI on the G200 | done |
| G3 | 86Box: patch 0126 | done |
| G4 | DOS-GL: the G200 under `DGL_WARP=1`; the half-texel shift; conformance and the games in Loop A on the emulated G200, the WARP against the CPU's setup | DOS-GL |
| G5 | Glide (`mg_config.warp`) on the emulated G200: the acceptance games' replays | MGA-Glide |
| G6 | The real card: one session (below) | bench-g400's PC with the G200 in it |
| G7 | `warp_g200` on by default (and DOS-GL's default); Echoes of the Void's presets re-based | all |

## G6: the session on the real G200

The owner swaps the G200 into bench-g400's PC (an IBM 300PL, Pentium II 400) once. Everything after that runs over
SSH (GLOS agent mode) as one prepared batch, and fixes go round without another swap. The card goes back when the
batch passes. The CPU is 400 MHz; the cycle counts scale to a Pentium II 266.

The batch, in order (each item answers an open question):

1. **WARPTRI**: the PIO load and start (does the G200 start from its instruction memory? Linux only ever used
   cached microcode); `starts=30 reload` with and without `noreset` (the third-load failure); `pio`; the cycles a
   triangle by DMA, by the CPU into the window, and by the CPU's own setup; `agpinfo`, then `agp=1` and `agp=2`
   (sideband, AGP_PLL).
2. **DOS-GL's conformance** (`make conform-bench`) with `DGL_WARP=1` and with `DGL_WARP=0`: textures (tw = log2 +
   28 and the half-texel shift), fog, specular, alpha, depth against the CPU's setup.
3. **DOSBench**: the GL scenes, GLQuake, Quake 2 and Half-Life's timedemos, both ways.
4. **Fifth Wheel**: FW1 and the FWP tests, both ways.
5. **Glide**: the acceptance games' replays with the WARP.
6. **GLOS windows**: a DOS-GL program windowed (the CPU's setup, as on the G400) and full-screen.

If the PIO load fails, the fallback is Linux's way: the microcode in conventional memory, WMISC = 0Bh, WIADDR =
its address | START through the list.

## Open, for the card

- Does a PIO-loaded pipe start (wbusy after the start), and does a soft reset keep or clear the instruction memory?
- Does the third-load failure exist on the G200?
- Are tw = log2 + 28 and the half-texel shift right on textured triangles?
- What are WR25, WR42 and WR60 (100h, FFFFh, FFFFh)? Do they depend on the depth's width?
- Does the G200 need the G400's clip barrier, DWGSYNC fences, or OPMODE rule around WARP triangles?
- What WARP clock does the BIOS leave (OPTION2's nowclkdiv and wclkdiv), and how fast is the WARP by PCI and by AGP?
