# The G400's WARP setup engine

The G400 can set up triangles itself. Its two WARP processors run Matrox's microcode on floating-point
screen-space vertices and write the trapezoid engine's registers, which MGA-Glide and DOS-GL otherwise compute on
the CPU (about 5,200 cycles a triangle of DOS-GL's 9,000 on a Pentium II 400; `docs/setup-perf.md`). The speed plan
(W0-W6) uses it.

## The microcode

linux-firmware's `matrox/g400_warp.fw` holds it: 16 pipes, MIT-licensed, Copyright 1999 Matrox Graphics Inc. The
file is Linux's binary ihex: records of a big-endian address and length, then the data, each padded to 4 bytes. There
is one record per pipe, numbered by Linux's `mga_drm.h`: bit 0 fog, bit 1 alpha, bit 2 specular, bit 3 two textures
(TGZ = 0 ... T2GZSAF = 15). Each pipe is 824-1,256 bytes. The instruction set is not public: the G400 specification
refers to a chapter 6 its public edition does not have.

## Driving it by PIO (no bus mastering)

From the G400 specification, chapter 3 and 4.1.9, and Linux's mga DRM driver (MIT) for the values:

1. **Load the pipe.** Write WIADDR2 to suspend and WMISC = 0, so microcode caching is off and the WARPs run from
   their own 2 KB instruction memories. Then write WIMEMADDR = 0 and the pipe through WIMEMDATA (WARP0) and
   WIMEMDATA1 (WARP1), two dwords a 64-bit word, low dword first.
   - WARP0's words read back at 2000h/2004h.
   - WARP1's read through WIMEMADDR1; WIMEMDATA1 reads 0.
2. **Set up the pipe:**
   - WGETMSB 0E00h, WVRTXSZ 1807h (8 dwords a vertex);
   - WACCEPTSEQ 0, 0, 0, 18000000h; WFLAG = WFLAG1 = 0;
   - WR56 = 12800.0f, WR49 = WR57 = WR53 = WR61 = 0;
   - WR54, WR62, WR52 and WR60 = 40h, OR'd with TEXWIDTH or TEXHEIGHT for a texture.
   - Then WIADDR2 = 0 | start.
   - STATUS shows wbusy and wbusy1 (bits 18, 19) from then on, waiting included.
3. **Context:** DWGCTL (TRAP or TEXTURE_TRAP, with atype and zmode), PLNWT, ALPHACTRL, FOGCOL, FCOL, TDUALSTAGE0/1,
   plus the texture registers as for a CPU-set-up draw.
4. **Vertices:** OPMODE.dmamod = 11 (vertex write); then each vertex's dwords into the DMA window (MGABASE1 +
   0-1BFFh), three vertices a triangle. Writing OPMODE's byte 0 ends a vertex sequence.
   - The vertex is Mesa's mga driver's: x and y in pixels with Mesa's subpixel offset (-0.5, -0.375); z from 0 to 1
     (Mesa's `depth_scale`); 1/w; the colour as BGRA bytes; the specular colour as BGR with the fog factor in the
     top byte; then u, v (and u1, v1 for T2 pipes) not divided by w.

A WARP that is not running doesn't take vertices: the bus FIFO fills and the next write stalls the PCI bus, which
hangs the PC. `WARPTRI` checks wbusy after starting and waits for FIFO room with a time limit.

## On silicon (bench-g400, `WARPTRI`, 2026-10-26)

- **Microcode:** WARP0's instruction memory reads back exactly.
- **Gouraud:** a triangle through the TGZ pipe matches the HAL's CPU setup but for 60 of 20,045 pixels, all on
  edges, with no colour differences beyond 24.
- **Cost:** 4,000 small Gouraud triangles take 1,323 cycles each through the WARP (24 uncached dword writes) and
  3,471 through the CPU setup. That includes the wait for the engine; the Pentium II runs at 400 MHz.
- **Open:**
  - depth with z from 0 to 1, and two triangles in a row (the run that would answer both hung the PC; a WARP
    start then read no busy bits);
  - textures, culling (WFLAG), fog, specular, alpha;
  - the T2 pipes.

## In 86Box (local patch 0122)

The model recognises the pipe by its microcode's length and CRC-32, then sets the triangle up with the HAL's own
setup, copied into the patch as `src/video/mgahal/`. That is what the silicon's triangles matched.
- **Vertices:** they come through the DMA window in vertex mode. Secondary DMA in vertex mode is still to do (W1).
- **Exec aliases:** 86Box took writes at 1DC0h-1DFFh as the start-the-engine alias of 1CC0h-1CFFh; on the G400
  they are the WARP's registers, and the patch excludes them.
- **Culling:** not modelled yet.

`WARPTRI` in Loop A on the G400 passes: Gouraud and depth identical to the CPU setup, and two triangles in one batch
drawn.
