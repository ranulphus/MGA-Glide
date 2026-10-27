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

## The DMA command stream

`warp.c` uses bus-master DMA by default (`warp_use_dma`), as Linux's mga DRM does.

- **The list.** A primary display list in general-purpose mode carries the register writes a list can reach: 1C00h-1DFFh
  and 2C00h-2DFFh, which covers drawing, texture and WARP. Each packet is a header of four register indexes (the
  register's offset from 1C00h or 2C00h over 4, the second range with bit 7) followed by the four values, padded
  with DMAPAD.
- **The vertices.** They go beside the list. Each run of them is handed to the secondary channel in vertex mode by a
  packet `DMAPAD, DMAPAD, SECADDRESS = start | 3, SECEND = end`, where the run ends: at the next register write or
  at a kick.
- **Routing.** While the stream is open (`mga_cmd_open`), `MGA_WR32` queues those writes (`mga_cmd_route`). Any other
  write first sends the list and waits until it has been fetched, then goes direct: OPMODE, the microcode memories,
  RST, ILOAD data through the DMA window, the CRTC. `MGA_WR32_DIRECT` always writes at once (the kick registers, the
  failure path). FIFO pacing is off while the list holds anything.
- **Memory.** There are two batches in conventional memory (`sys_dos_alloc`; the physical address is the segment
  times 16 under DOS and GLOS), each 8 KB of list and 24 KB of vertices. A kick writes PRIMADDRESS = the list's
  address (general purpose) and PRIMEND = its end, which starts the channel and clears STATUS.endprdmasts (bit 17).
- **Waiting.** The CPU fills one batch while the chip fetches the other, and waits only for that fetch, with a time
  limit. `engine_sync` and `engine_idle` send the list first.
- **Failure.** On a timeout the stream is closed, the WARP suspended (WIADDRNB2, bypassing the FIFO) and the engine
  reset. `warp_abort` does the same without waiting, for fault handlers.
- **Mixed setup.** A WARP triangle marks the CPU setup's shadow registers stale (`setup_stale`). When the CPU setup
  writes TEXWIDTH and TEXHEIGHT its own way (`setup_wrote_texsize`), the WARP's are written again before its next
  vertices. `setup_dwgctl` writes DWGCTL under the setup's shadow for WARP draws.
- **Waits are timed, not counted.** In 86Box the CPU can read STATUS far faster than the engine works through a heavy
  batch; a limit of 2 million reads (about a second on the bench) failed there once.

On bench-g400 the stream cut DOSBench's DMA transfers from 257,655 to 80,121 for 20 million triangles, and its
OpenGL score from 1,176 to 1,446.

## Glide (W5)

MGA-Glide sets a triangle up through the WARP (`src/glide/draw.c`, `warp_tri`) when it is drawn as one band:
untextured, or textured with one level for all its rows. Bands of rows, a fog fit per band and two maps stay on the
CPU, as do lines and points. `warp=0` (MGAGLIDE) keeps everything on the CPU, with the Voodoo's exact edge rule; the
WARP's edges differ slightly, which conformance t04 sees (its `warp` block masks edges on the G400 family).

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
- **The clip (2026-10-27).** When DOS-GL's E9 changed the scissor between quads, the second triangle of each quad
  (the second WARP's) was cut by the next clip: host writes to CXBNDRY, YTOP and YBOT don't wait for the
  triangles the WARPs are still setting up. Linux's mga DRM writes DWGCTL and LEN|EXEC = 80000000h (a
  zero-length draw) twice before every clip on a G400 (`mga_emit_clip_rect`). The HAL's `emit_clip` does the same
  while WARP triangles are pending (`mga_warp_pending`, set by `warp_vertices`, cleared by `engine_sync`). Every
  flat case then matches 86Box; texture, blend and depth registers needed no barrier (the Arena's batches drew
  right without one).
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
