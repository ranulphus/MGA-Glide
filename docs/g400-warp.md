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
- **Memory.** The list and the vertices are two rings in one block of conventional memory (`sys_dos_alloc`; the
  physical address is the segment times 16 under DOS and GLOS): 256 KB when DOS has it, halved down to 32 KB until it
  does, an eighth of it for the list (`warp_ring_bytes`, `warp_list_div`). Glide asks for 128 KB, half of it list,
  as it writes many registers a triangle (its `dma_ring` and `dma_list` options).
- **Kicks.** A kick hands the chip what is new by writing PRIMEND further on; the specification allows that while the
  channel is still fetching, provided the last packet before each end is DMAPADs (G400 specification 4-16), which it
  is. PRIMADDRESS is written only with the chip idle: at the first kick, and after a wait for everything. A kick
  comes every 6 KB of vertices (`warp_kick_bytes`), and before a wait.
- **Waiting.** As Linux's DRM ages its buffers, a kick's vertices are free once PRIMADDRESS is past the kick's end in
  the list (the primary channel goes on only when a run's secondary transfer is done), and every kick's once the
  channels have ended there (STATUS.endprdmasts with PRIMADDRESS at PRIMEND; endprdmasts alone can still read set
  just after a kick that moved PRIMEND on). The CPU reads them only when a ring is full, and waits with a time limit.
  The list starts again from its beginning only when the chip has fetched all of it; `engine_sync` and `engine_idle`
  send everything and wait for it, which also starts both rings again. A soft reset (`engine_reset`) closes the
  stream (`mga_reset_hook`).
- **Failure.** On a timeout the stream is closed, the WARP suspended (WIADDRNB2, bypassing the FIFO) and the engine
  reset. `warp_abort` does the same without waiting, for fault handlers.
- **Mixed setup.** A WARP triangle marks the CPU setup's shadow registers stale (`setup_stale`). When the CPU setup
  writes TEXWIDTH and TEXHEIGHT its own way (`setup_wrote_texsize`), the WARP's are written again before its next
  vertices. `setup_dwgctl` writes DWGCTL under the setup's shadow for WARP draws.
- **Waits are timed, not counted.** In 86Box the CPU can read STATUS far faster than the engine works through a heavy
  batch; a limit of 2 million reads (about a second on the bench) failed there once.

On bench-g400 the stream cut DOSBench's DMA transfers from 257,655 to 80,121 for 20 million triangles, and its
OpenGL score from 1,176 to 1,446. The rings, against two batches of 24 KB of vertices that took turns, made DOS-GL's
scenes 18% to 36% faster (Arena 19.7 to 23.5 fps, Race 32.5 to 44.3): the CPU had waited whenever a batch was
full and the other not yet fetched, though the bus was busy only about 60% of the time on average.

## Strips and fans (the ACCEPT sequencer)

The WARPs take strips and fans by their ACCEPT sequencer (WACCEPTSEQ, G400 specification 3-260): after the first
triangle's three vertices, each vertex goes to the WR bank `seqdst[seqptr]` names, `seqptr` cycling through
`seqlen + 1` of them, and the three banks make the next triangle. Mesa's mga driver defined the values for pipes
without a second map, `MGA_WA_TRIFAN_T0` (01000408h: banks 8 and 16 in turn, the first vertex staying) and
`MGA_WA_TRISTRIP_T0` (02010200h: banks 0, 8 and 16 in turn, the oldest going), but never used them. On bench-g400
(WARPTRI `seq`, 2026-10-27) both draw pixel for pixel the same triangles as a list, and lists work again after
WACCEPTSEQ is written back to 18000000h. The WARPs cull nothing here (WFLAG 0), so the alternating winding of
the sequencer's triangles doesn't matter.

The T2 pipes' vertices are 10 dwords, in banks 16 registers apart: fan 01000810h (banks 16 and 32), strip
02020400h (0, 16, 32), lists 1E000000h. Both draw the same triangles as a list on bench-g400 (WARPTRI `seq` at
`pipe=8` and `pipe=9`). Mesa's `MGA_WA_TRISTRIP_T0T1`, 02010400h, puts the second bank at 8 and is wrong.

`warp_reserve_seq` and `warp_commit_seq` send one strip or fan as a run of vertices between the two WACCEPTSEQ
writes: each vertex once, not three times a triangle, for every pipe. 86Box patch 0123 models the sequencer
(banks of 16 for vertices of more than 8 dwords).

## Waiting for the WARPs

STATUS can read idle while the WARPs still have triangles to set up: on bench-g400 a batch of 256 WARP triangles read
idle 12.6 us after its kick. While WARP triangles are pending (`mga_warp_pending`), `engine_sync` and `engine_idle`
write a tag to DWGSYNC, which takes it only once the drawing engine has finished what was sent before it, and wait
for that too.

On bench-g400 such a DWGSYNC write is now and then never taken: DWGSYNC keeps the tag before for good, with
everything fetched (PRIMADDRESS at PRIMEND), the bus FIFO empty, the drawing engine idle and both WARPs at their
idle address (20h). Written after every kick (an earlier version of the rings used them to free room), it happened
about once in 3,000 kicks, each an `engine_sync` timeout and a reset: 13 in 617 frames of Glide, one in DOS-GL's
four scenes, which cost Space 4 fps. The kicks don't write DWGSYNC now, and a fence still not taken when everything
else has been idle for 20 ms and 4,096 looks is let go (`engine_fence_stuck`) rather than timing out into a reset.
The DRM writes DWGSYNC only around clears and swaps, never between vertex buffers. `engine_timeout_diag` keeps the
first timeout's registers (Glide's MGL-TIMEOUT line).

GLOS's own HAL takes the engine back from a windowed program without that fence, and the program's last WARP
triangles were then set up onto GLOS's target (DOS-GL's t27 in a window copied an empty frame). DOS-GL therefore
uses the CPU's setup in GLOS windows until GLOS fences the WARPs at a hand-over.

## Glide (W5)

MGA-Glide sets every triangle up through the WARP (`src/glide/draw.c`, `warp_tri`), a band of levels as the whole
triangle under the clip narrowed to its rows. A surface's passes must never mix the WARP's setup and the CPU's: their
depths and edges differ slightly, so on bench-g400 the Arena's lightmap pass flickered and triangle edges showed
through where they mixed. Bands with their own fog fit stay on the CPU (their vertices' factors leave 0..255, which a
WARP vertex's fog byte can't carry), as do two maps, lines and points. `warp=0` (MGAGLIDE) keeps everything on the
CPU, with the Voodoo's exact edge rule; the WARP's edges differ slightly, which conformance t04 sees (its `warp` block
masks edges on the G400 family). The texture registers and ALPHACTRL are written only when they change (each write
also ends a vertex run in the DMA stream).

### The fast path

Most of what `mg_draw_tri` works out for a triangle depends only on the state: the combine and alpha plans,
alpha_setup's ALPHACTRL, validation, DWGCTL, the level bound and its texture registers. The fast path (`fp_prepare`,
`fp_tri`) works these out once a state generation (`mg_state_gen`, which every Glide call but the drawing ones moves)
and then sends a triangle the WARP sets up whole at one level for the cost of its vertices:
- **Vertices** come from a cache by address. A slot keeps the GrVertex it was made from (compared word by word, so a
  vertex changed in place misses) and its setup vertex: `vtx()` and alpha_setup's rules, then `wvtx_base`. Soft
  combines whose per-vertex value is the clamped iterated colour or alpha (a modulate by the iterated colour, as
  DOSBench's) use the iterated rule: the same values without the combine evaluator.
- **Levels** are bound once a generation each (`fp.lv[]`). Between two levels of one texture only TEXORG and TEXCTL
  are written (`tex_emit_change`).
- **The map coordinates** are scaled and moved by whole periods as `draw_band` does, then converted by `wvtx_uv`
  (which `warp_tri` uses too) and written with the cached part straight into the DMA stream.

- **Bands of levels:** a triangle whose bands take different levels is drawn as `draw_band` draws it, the whole
  triangle at each run of one level under the clip narrowed to its rows (`MGL-STAT fastbands=`). The bias and scale
  terms of the level (`lod_offset`) are worked out once a generation.

Bands of fog fits, subdivided combines, specular, keyed textures, the hardware mip window and chips without the WARP
go the general way. The frames are the same with and without it (`fast=0`): GTA, Screamer Rally and DOSBench's Glide
scenes in 86Box. On bench-g400 (DOSBench Glide, fps, general path / fast path): Arena 5.6 / 9.2, Race 11.4 / 19.6,
Space 7.4 / 12.9, City 6.4 / 9.6.

Watcom does 64-bit arithmetic by calls: the W code for table fog (`mg_wcode_from_oow`), the level's
`log2x2_floor` and the cull's area work in 32 bits where the values allow, with the same results (checked against the
64-bit forms on the host).

## AGP

The BIOS leaves bench-g400's 440BX with AGP off (its AGPCMD, A8h, reads 0) and the G400's AGP_CMD (F8h) at 1x with
sideband addressing. With both set to 2x and sideband addressing, PRIMEND's and SECEND's bit 1 make the channels
fetch by AGP cycles (WARPTRI `agp=N`, 2026-10-27):
- **Bandwidth:** lists of DMAPAD packets go at 150 MB/s, against 46 by PCI (145 at 1x). By PCI the WARP takes at
  most about 430,000 triangles a second (96 bytes each).
- **Coherency:** AGP reads don't snoop the CPU's caches. With the caches flushed before each kick (2 MB read) the
  WARP draws exactly as by PCI (`agpdraw`); without, nothing (`noflush`).

So the rings must be write-combining, which takes ring 0 (the MTRRs). Under GLOS (API 1.4) the HAL asks for it:
- `dma_setup` allocates the rings 64 KB larger and aligns them to 64 KB, and asks GLOS to make them write-combining
  (`sys_cache_wc`: `GLOS_FN_CACHE`, which sets the fixed MTRRs below A0000h; GLOS gives them back when the program
  ends). Without GLOS, or when GLOS can't, the block is freed and the plain rings are allocated, fetched by PCI.
- With write-combining rings, AGP is turned on: the host bridge's AGP capability first, then the card's AGP_CMD,
  at the fastest rate both have, with sideband addressing if both have it. `warp_stop` puts both commands back as
  they were; the next start turns AGP on again.
- A locked instruction drains the CPU's write-combining buffers before each kick. The list's packet headers are
  written once, whole (a read of write-combining memory is uncached).
- `warp_use_agp` (DOS-GL: `DGL_AGP=0`; Glide: `agp=0`) keeps the plain rings.
- Glide games under DOS/4GW don't get them yet: DOS/4GW answers DPMI 0A00h itself, so the OVL never finds GLOS's
  entry (`warp_wc_status` -2, `sys_cache_wc_err` -1). Glide is limited by the CPU, not the bus.

On bench-g400 under GLOS, DOS-GL's scenes (frames per second):

| Rings | Arena | Race | Space | City |
|---|---|---|---|---|
| cached, PCI | 25.73 | 45.43 | 39.26 | 20.98 |
| write-combining, PCI | 26.37 | 46.11 | 40.56 | 21.25 |
| write-combining, AGP | 26.78 | 47.16 | 40.76 | 21.54 |

With the rings the bus is no longer what limits these scenes, so AGP adds about 4%. Write-combining memory wants
each line written once, in order: DOS-GL's fast path wrote a vertex and then went back for its texture coordinates'
wrap and flat shading's colour, and was 5% to 11% slower by AGP than by PCI until it wrote each word once.

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
