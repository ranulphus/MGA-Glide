# Triangle setup: speed without changing a pixel

The HAL's `setup_triangle` and its callers (Glide's draw path, DOS-GL's
vertex path) were made cheaper in 2026-10 without changing any register
value a draw sees. This note is what that rests on and how to measure it.

## What changed

- **Float to integer.** A C cast on the x87 is FNSTCW, FLDCW, FISTP, FLDCW;
  FLDCW stalls a P6. `mga/fp.h` has `mga_irint` (FISTP in the current
  mode), `mga_ifloor`, `mga_itrunc` and 64-bit forms, GCC asm and Watcom
  `#pragma aux`; FISTP lands on floor or ceiling, so one comparison makes
  floor and truncation exact in any rounding and precision mode.
  `mga/setupconv.h` holds the setup's conversions with a fast path only
  where it equals the old code and the old code, verbatim, outside it.
  `mga_irint_nearest` is `lrint` for code inside `FPU_ENTER`.
- **Arithmetic.** Plane geometry once per triangle, the prescale from exact
  thresholds, edge divisions from a double estimate corrected in integers
  (the 64-bit division only where that is not proven), Voodoo edges by
  shifts, texture-adjust divides only on wrapping axes.
- **Writes.** Registers that keep their value across draws (increments,
  DWGCTL without its write-time bits, TEXWIDTH/TEXHEIGHT and TMR0-5, per
  map on the G400) are not written again with the same value.
  `engine_init` clears what setup knows; the engine's fills, TLUT loads and
  ILOADs forget DWGCTL (`setup.h`).
- **FIFO.** `fifo_need` (inline in `hal.h`) is the reservation's fast path;
  setup takes one slot per write, so FIFOSTATUS is read only when the slots
  the last read found free are used up.

## How it is checked

- `tests/unit/test_setupgold.c` (in `tests-host`, 64-bit and 32-bit x87):
  the drawing registers' state at every start of a draw, hashed, for twelve
  scenarios (G100/G200/G400, both edge rules, Z32, dual texturing, extreme
  and NaN inputs, coplanar fans, fills/TLUT/ILOAD/present/init in between),
  against goldens from the HAL before the work. It also checks FIFO pacing
  (each write has a reserved slot; no reservation beyond the FIFO's depth)
  and counts the FIFOSTATUS reads `fifo.c` would make.
- `tests/unit/fpcheck.inc`: the helpers against references and the setup
  conversions against copies of the old code, bit for bit, in every x87
  rounding mode; `test_fp` on the host, `make loopa-fpcheck` for Watcom and
  DJGPP in 86Box.
- Pixel-exact comparisons of whole runs: `tools/loopa/samepix.py BASE GOT`.

## Measuring

`make runtime-prof` (Glide) and DOS-GL's `make PROF=1` build the stage
timers of `hal/include/mga/prof.h`; `MGAGLIDE stats=N` and `DGL_STATS=2`
print `MGL-PROF`/`DGL-PROF` lines, and `tools/perf/profsum.py LOG...` sums
them into cycles per triangle per stage. In 86Box those are the emulated
CPU's cycles: use them to compare builds, never as hardware numbers.

Measured in 86Box (emulated P2-350, cycles per triangle in the library,
waits excluded), before -> after:

| Workload | Before | After |
|---|---|---|
| GLQuake demo1, 640x480, G450 | 9,527 | 6,503 |
| GLQuake demo1 -mtex, G450 | 11,657 | 8,311 |
| Quake 2 q2bench1, G450 | 9,809 | 6,669 |
| Half-Life hlbench1, gl_vbo 0 / 1 | 9,737 / 12,411 | 6,854 / 9,027 |
| GTA (Glide), HAL setup only | 4,475 | 3,255 |

Register writes per triangle fell by about a fifth (GLQuake 40.4 -> 31.4,
GTA 38.7 -> 27.2).

## 86Box behaviours found on the way

- Its FISTP rounds a tiny negative (|v| < 2^-53) to +1 in round-to-nearest
  (`x87_fround` takes `floor(v + 1.0)`); the helpers step down twice if
  they must, so they stay exact there. DJGPP's `lrint` is not affected;
  Quake's own FISTP paths are, in the emulator only.
- Its x87 arithmetic rounds in the guest's mode or the host's nearest,
  depending on whether the recompiler has taken the code: only
  round-to-nearest (what `FPU_ENTER` sets) is comparable across runs.
- With its 64K-entry queue the CPU can run a whole frame ahead of the
  engine; an `engine_sync` behind a very deep backlog can exceed its
  timeout (and reset). Real cards' 16- and 64-entry FIFOs cannot.

## For the bench (not provable in 86Box)

1. The increment registers, TEXWIDTH/TEXHEIGHT and TMR0-5 keep their
   values across TRAP and TEXTURE_TRAP draws (the shadows rely on it).
2. VBE 4F07 through the genuine BIOS leaves the drawing registers alone.
3. FIFO pacing never overruns on the G400/G450 (Loop B).
4. FPCHECK passes on the bench CPUs.
5. `MGL-PROF`/`DGL-PROF` there give the real cost of register writes and
   FIFOSTATUS reads per triangle.

devserver's G200eR2 (2026-10-02, `docs/loop-c-results.md`): item 1 holds
for DOS-GL's conformance tests and the rigperf scenes (frames byte-identical
before and after); item 3 cannot be seen on that chip (its FIFOSTATUS does
not count queued writes); for item 5 a FIFOSTATUS read costs 2.06 us there
and a register write 212 ns.

bench-g400 (a G400 with a Pentium II at 398 MHz, under GLOS, 2026-10-26):

- **Item 1** holds: DOS-GL's conformance suite passes except where the chip's texture cache and texel centres
  differ (`docs/g400-dual-texture.md` §7). The windowed and full-screen frames are identical (GLOS's bench glwin).
- **Item 3:** none of the jobs overran the FIFO. Every DOSBench test ran its frames, Glide and OpenGL alike.
- **Item 4:** FPCHECK passes.
- **Item 5:** DOS-GL's PROF build (DOSBench G1ARNA, G3SPAC and M2BUNY, `DGL_STATS=2`) gives the library about
  9,000 cycles per drawn triangle without waits.

  | Stage | G1ARNA | G3SPAC | M2BUNY |
  |---|---|---|---|
  | xform | 1,892 | 1,734 | 3,050 |
  | proj | 910 | 773 | 1,337 |
  | clip | 531 | 567 | 698 |
  | setup | 1,166 | 1,167 | 511 |
  | splane | 765 | 782 | 437 |
  | sinc | 1,813 | 1,787 | 1,062 |
  | strap | 1,487 | 2,376 | 1,676 |
  | FIFO waits | 621 | 679 | 288 |
  | writes / FIFOSTATUS reads per triangle | 26.9 / 3.9 | 36.1 / 4.2 | 25.0 / 1.6 |

  The bus and the card are not the limit: FIFO waits are 3-6% of the time. The CPU's arithmetic is, about 3,300
  cycles for transform, projection and clipping and 5,200 for the setup stages. DOSBench's scenes run at 6-11 fps
  under OpenGL and 3-5 fps under Glide (DOSBench `results/bench-g400.jsonl`). The engine's own setup (WARP) and the
  vertex path are where the time would come back.
