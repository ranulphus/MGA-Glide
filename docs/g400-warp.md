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

1. **Load the pipe.** Write WIADDR2 to suspend, then take a soft reset (RST; see "The third load" below). Write
   WMISC = 0, so microcode caching is off and the WARPs run from their own 2 KB instruction memories. Then write WIMEMADDR = 0 and the pipe through WIMEMDATA (WARP0) and
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

## By bus-master DMA

`warp.c` uses DMA by default (`warp_use_dma`). The vertices go into one of two 16 KB buffers in conventional memory
(`sys_dos_alloc`; the physical address is the segment times 16 under DOS and GLOS), and a full buffer, or any
register write (`mga_dma_flush`, from `fifo_reserve`, `engine_sync` and `engine_idle`), sends it:
- PCI COMMAND's bus-master bit is set once.
- PRIMADDRESS = the buffer's address | 3 (vertex write), then PRIMEND = its end, which starts the channel and clears
  STATUS.endprdmasts (bit 17).
- The next kick, and every flush, first waits for endprdmasts, with a time limit; on a timeout the WARP is
  suspended (WIADDRNB2, bypassing the FIFO) and the engine reset.

## On silicon (bench-g400, `WARPTRI`, 2026-10-26 and 27)

- **Microcode:** WARP0's instruction memory reads back exactly.
- **Gouraud:** a triangle through the TGZ pipe matches the HAL's CPU setup but for 60 of 20,045 pixels, all on
  edges, with no colour differences.
- **Two triangles in a row** (the G400's two WARPs take alternate triangles): both drawn, 4,800 pixels each.
- **Depth:** two triangles crossing in depth (ZI, less-than, z from 0 to 1) match the CPU setup but for 101 of
  34,281 pixels on edges and 20 colour differences, along the crossing line.
- **The two vertex paths** draw the same frame (CRC d820ec36): through the DMA window by the CPU, and by bus-master
  DMA (the primary channel in vertex mode, two 16 KB buffers in conventional memory, under GLOS).
- **Cost:** 4,000 small Gouraud triangles, timed with the wait for the engine (the Pentium II runs at 400 MHz):

  | Path                           | Cycles a triangle |
  |--------------------------------|------------------:|
  | CPU setup (`setup_triangle`)   | 3,434-3,475       |
  | WARP, vertices by the CPU      | 1,736             |
  | WARP, vertices by DMA          | 776               |

  The first WARP run (2026-10-26) took 1,323 by the CPU, writing the window directly; the HAL's `warp.c` adds a
  bounded FIFO wait for each vertex.
- **The hang (2026-10-26)** came from vertices sent to a WARP whose start had read no busy bits. `warp_select` now
  checks wbusy after the start and gives up with the WARP suspended; the FIFO waits are bounded.
- **The third load (2026-10-27).** After a soft reset (RST), the instruction memories can be loaded and the WARPs
  started twice. After the third load, the start reads no busy bits (STATUS 00020000, WIADDRNB and WIADDRNB1 both 0),
  and so does every later one until the next soft reset.
  - **Scope:** it happens at every pipe, with or without the DRM's three DMAPAD writes before the start, by DMA and
    by PIO, and whether the loads come in one program or in programs run one after another.
  - **Not affected:** restarting without a load (the same pipe) always works.
  - **Fix:** `warp_select` takes a soft reset before each load (`engine_reset`; `engine_resets` moves, so callers
    write their drawing state again).
  - **Evidence:** WARPTRI `starts=30 reload` (pipes 0 and 7 alternately) then started 30 of 30 by DMA and 20 of 20
    by PIO, and every start drew its triangle. Without the reset, every third start failed (`noreset`).
  - **Why it matters:** the two pipes DOS-GL uses (TGZSAF and T2GZSAF, 1,000 and 1,256 bytes) don't fit in the 2 KB
    instruction memories together, so switching between one and two textures is always a load.
  - **The DRM** never meets this: it runs the pipes from memory with microcode caching (WMISC.wucodecache = 1).
- **STATUS<22:20>** (warpbfpath, wfipath, wfopath) point at WARP1 after an odd number of triangles, and keep that
  across suspend and start.
- **Open:**
  - textures, culling (WFLAG), fog, specular, alpha;
  - the T2 pipes.

## In 86Box (local patch 0122)

The model recognises the pipe by its microcode's length and CRC-32, then sets the triangle up with the HAL's own
setup, copied into the patch as `src/video/mgahal/`. That is what the silicon's triangles matched.
- **Vertices:** they come through the DMA window, the primary channel or the secondary channel, in vertex mode.
- **Exec aliases:** 86Box took writes at 1DC0h-1DFFh as the start-the-engine alias of 1CC0h-1CFFh; on the G400
  they are the WARP's registers, and the patch excludes them.
- **The third load:** modelled. A start after three loads since a soft reset leaves the WARPs dead (no busy bits,
  no vertices taken), and the log says so. The soft reset stops the WARPs.
- **Culling:** not modelled yet.

`WARPTRI` in Loop A on the G400 passes: Gouraud and depth identical to the CPU setup, and two triangles in one batch
drawn.
