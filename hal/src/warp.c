/* warp.c - the G400's WARP setup engine by PIO (docs/g400-warp.md).
 *
 * The microcode is linux-firmware's matrox/g400_warp.fw (warp_g400.h, MIT,
 * Copyright 1999 Matrox Graphics Inc.). With microcode caching off the two
 * WARPs run from their own 2 KB instruction memories, written through
 * WIMEMADDR and WIMEMDATA/WIMEMDATA1; the values written around a pipe are
 * those of Linux's mga DRM driver (mga_warp_init, mga_g400_emit_pipe).
 * Vertices go through the DMA window with OPMODE in vertex mode. */
#include "mga/warp.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "warp_g400.h"

uint32_t warp_tris, warp_failures, warp_dma_kicks;
int warp_use_dma = 1;
int warp_start_pads = 3;
int warp_reset_before_load = 1;
uint32_t warp_diag[5];

/* Bus-master DMA (the primary channel in vertex mode: PRIMADDRESS's low
 * bits select it; G400 specification 4.1.9): two buffers in conventional
 * memory, whose physical address is the linear one (under DOS and under
 * GLOS alike). One is filled while the chip fetches the other. */
#define DMA_BYTES 16384u
static struct { uint32_t *p; uint32_t phys; } dbuf[2];
static int dma_state;               /* 0 not tried, 1 in use, -1 not available */
static int dcur;                    /* the buffer being filled */
static uint32_t dused;              /* its bytes */
static int inflight;                /* a kick the chip may still be fetching */

static void dma_flush(void);

static int cur_pipe = -1;           /* in the instruction memories */
static int sel_pipe = -1;           /* the one warp_select asked for */
static int running;                 /* started, not suspended */

/* engine_init and engine_resync (a reset, or GLOS's GPU lock changing
 * hands) leave OPMODE unknown: then nothing is known of the WARPs either. */
static void check_lost(void)
{
    if (mga_opmode == MGA_OPMODE_UNKNOWN) {
        running = 0;
        cur_pipe = -1;
    }
}
static uint32_t dmawin_pos;

int warp_available(void)
{
    return mga.family == MGA_FAMILY_G400;
}

/* The pipe's record in warp_g400_fw: 0, or -1 when there is none. */
static int pipe_code(int pipe, const unsigned char **code, int *len)
{
    unsigned long off = 0;
    int i = 0;
    while (off + 6 <= sizeof warp_g400_fw) {
        int n = (warp_g400_fw[off + 4] << 8) | warp_g400_fw[off + 5];
        if (!n)
            break;
        if (i++ == pipe) {
            *code = warp_g400_fw + off + 6;
            *len = n;
            return 0;
        }
        off = (off + 6 + (unsigned long)n + 3) & ~3ul;
    }
    return -1;
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Room for n writes in the bus FIFO, waiting a limited time: unlike
 * fifo_reserve this never writes into a full FIFO. */
static int fifo_room(int n)
{
    long spin;
    for (spin = 0; spin < 400000L; spin++)
        if ((int)(MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask) >= n) {
            mga_fifo_free = 0;          /* (fifo_reserve reads it again) */
            return 0;
        }
    return -1;
}

static void dma_mode(int vertex)
{
    const uint32_t want = vertex ? OPMODE_DMAMOD_VERTEX : 0;
    if (mga_opmode == want)
        return;
    engine_sync(200000);                /* OPMODE is not queued behind drawing */
    MGA_WR32(MGAREG_OPMODE, want);
    mga_opmode = want;
    dmawin_pos = 0;
}

static void suspend(void)
{
    if (running) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_WIADDR2, WMODE_SUSPEND);
        running = 0;
    }
}

void warp_stop(void)
{
    if (mga_dma_pending)
        dma_flush();
    dma_mode(0);
    suspend();
}

static void fail(void);

/* Wait until the chip has fetched the last kick (STATUS.endprdmasts). */
static int dma_wait(void)
{
    long spin;
    if (!inflight)
        return 0;
    for (spin = 0; spin < 2000000L; spin++)
        if (MGA_RD32(MGAREG_STATUS) & (1u << 17))
            break;
    inflight = 0;
    return spin < 2000000L ? 0 : -1;
}

static void dma_kick(void)
{
    if (!dused)
        return;
    if (dma_wait() != 0) {
        fail();
        return;
    }
    MGA_WR32(MGAREG_PRIMADDRESS, dbuf[dcur].phys | 3u);     /* vertex write */
    MGA_WR32(MGAREG_PRIMEND, dbuf[dcur].phys + dused);      /* PCI memory; starts the channel */
    inflight = 1;
    warp_dma_kicks++;
    dcur ^= 1;
    dused = 0;
}

/* mga_dma_flush: everything buffered sent, and fetched. */
static void dma_flush(void)
{
    mga_dma_pending = 0;
    dma_kick();
    if (dma_wait() != 0)
        fail();
    mga_fifo_free = 0;
}

static void dma_setup(void)
{
    uint16_t seg;
    int i;
    uint32_t cmd;
    dma_state = -1;
    if (!warp_use_dma)
        return;
    for (i = 0; i < 2; i++) {
        dbuf[i].p = (uint32_t *)sys_dos_alloc(DMA_BYTES, &seg);
        if (!dbuf[i].p)
            return;
        dbuf[i].phys = (uint32_t)seg << 4;
    }
    cmd = mga_pci_read32(mga.bus, mga.dev, mga.fn, 0x04);
    if (!(cmd & 4u))
        mga_pci_write32(mga.bus, mga.dev, mga.fn, 0x04, cmd | 4u);   /* bus master */
    mga_dma_flush = dma_flush;
    dma_state = 1;
}

static void fail(void)
{
    mga_dma_pending = 0;
    inflight = 0;
    dused = 0;
    warp_failures++;
    MGA_WR32(MGAREG_WIADDRNB2, WMODE_SUSPEND);      /* (bypassing the FIFO) */
    running = 0;
    cur_pipe = -1;
    engine_reset();                     /* (OPMODE unknown after it) */
}

int warp_select(int pipe)
{
    const unsigned char *code;
    int len, words, i;
    long spin;
    uint32_t st = 0;
    if (!warp_available() || pipe_code(pipe, &code, &len) != 0)
        return -1;
    if (!dma_state)
        dma_setup();
    if (mga_dma_pending)
        dma_flush();
    check_lost();
    sel_pipe = pipe;
    if (running && pipe == cur_pipe)
        return 0;
    warp_diag[0] = MGA_RD32(MGAREG_STATUS);
    dma_mode(0);
    suspend();
    engine_sync(200000);
    if (pipe != cur_pipe) {
        /* On a real G400 the third load of the instruction memories after a
         * soft reset leaves the WARPs dead (the start reads no busy bits), at
         * every pipe: so a soft reset before each load (docs/g400-warp.md).
         * It clears the drawing registers: callers see engine_resets move. */
        if (warp_reset_before_load) {
            engine_reset();
            dma_mode(0);
        }
        MGA_WR32(MGAREG_WMISC, 0);                  /* no microcode caching: the instruction memories */
        words = (len + 7) / 8;
        MGA_WR32(MGAREG_WIMEMADDR, 0);
        for (i = 0; i < words; i++) {
            MGA_WR32(MGAREG_WIMEMDATA, le32(code + 8 * i));
            MGA_WR32(MGAREG_WIMEMDATA + 4, le32(code + 8 * i + 4));
        }
        MGA_WR32(MGAREG_WIMEMADDR, 0);
        for (i = 0; i < words; i++) {
            MGA_WR32(MGAREG_WIMEMDATA1, le32(code + 8 * i));
            MGA_WR32(MGAREG_WIMEMDATA1 + 4, le32(code + 8 * i + 4));
        }
        cur_pipe = pipe;
    }
    fifo_reserve(12);
    MGA_WR32(MGAREG_WGETMSB, 0x00000E00);
    MGA_WR32(MGAREG_WVRTXSZ, (pipe & WARP_T2) ? 0x00001E09 : 0x00001807);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, (pipe & WARP_T2) ? 0x1E000000 : 0x18000000);
    MGA_WR32(MGAREG_WFLAG, 0);
    MGA_WR32(MGAREG_WFLAG1, 0);
    MGA_WR32(MGAREG_WR(56), 0x46480000);           /* 12800.0f, as the DRM writes */
    MGA_WR32(MGAREG_WR(49), 0);
    MGA_WR32(MGAREG_WR(57), 0);
    MGA_WR32(MGAREG_WR(53), 0);
    fifo_reserve(1);
    MGA_WR32(MGAREG_WR(61), 0);
    for (i = 0; i < warp_start_pads; i++) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_DMAPAD, 0xFFFFFFFFu);
    }
    warp_diag[1] = MGA_RD32(MGAREG_STATUS);
    fifo_reserve(1);
    MGA_WR32(MGAREG_WIADDR2, 0 | WMODE_START);
    engine_sync(200000);
    for (spin = 0; spin < 200000L && !((st = MGA_RD32(MGAREG_STATUS)) & (STATUS_WBUSY | STATUS_WBUSY1)); spin++)
        ;
    warp_diag[2] = st;
    warp_diag[3] = MGA_RD32(MGAREG_WIADDRNB);
    warp_diag[4] = MGA_RD32(MGAREG_WIADDRNB1);
    if (!(st & (STATUS_WBUSY | STATUS_WBUSY1))) {
        fail();
        return -1;
    }
    running = 1;
    warp_tex_sizes(0, 0, 0, 0);
    return 0;
}

void warp_tex_sizes(uint32_t w0, uint32_t h0, uint32_t w1, uint32_t h1)
{
    fifo_reserve(4);
    MGA_WR32(MGAREG_WR(54), w0 | 0x40);            /* | MGA_G400_WR_MAGIC */
    MGA_WR32(MGAREG_WR(62), h0 | 0x40);
    MGA_WR32(MGAREG_WR(52), w1 | 0x40);
    MGA_WR32(MGAREG_WR(60), h1 | 0x40);
}

static uint32_t mesa_wh(int lg)
{
    return ((uint32_t)((1 << lg) - 1) << 18) | ((uint32_t)((2 - lg) & 63) << 9) | ((uint32_t)(lg + 11) & 63);
}

void warp_tex(int map, int log2w, int log2h, uint32_t texctl2)
{
    const uint32_t w = mesa_wh(log2w), h = mesa_wh(log2h);
    if (!map) {
        fifo_reserve(4);
        MGA_WR32(MGAREG_TEXWIDTH, w);
        MGA_WR32(MGAREG_TEXHEIGHT, h);
        MGA_WR32(MGAREG_WR(54), w | 0x40);
        MGA_WR32(MGAREG_WR(62), h | 0x40);
    } else {
        fifo_reserve(6);
        MGA_WR32(MGAREG_TEXCTL2, texctl2 | TEXCTL2_MAP1);
        MGA_WR32(MGAREG_TEXWIDTH, w);
        MGA_WR32(MGAREG_TEXHEIGHT, h);
        MGA_WR32(MGAREG_TEXCTL2, texctl2);
        MGA_WR32(MGAREG_WR(52), w | 0x40);
        MGA_WR32(MGAREG_WR(60), h | 0x40);
    }
    setup_invalidate();                 /* (setup_triangle's TEXWIDTH/TEXHEIGHT shadows) */
}

int warp_vertices(const mga_wvtx *v, int n)
{
    int i, k, dw;
    check_lost();
    if (!running && (sel_pipe < 0 || warp_select(sel_pipe) != 0))
        return -1;
    dw = (cur_pipe & WARP_T2) ? 10 : 8;
    if (dma_state > 0) {
        for (i = 0; i < n; i++) {
            uint32_t *d;
            union { float f; uint32_t u; } c;
            if (dused + (uint32_t)dw * 4u > DMA_BYTES)
                dma_kick();
            if (!running)
                return -1;              /* (a failed kick) */
            d = dbuf[dcur].p + dused / 4u;
            c.f = v[i].x; d[0] = c.u;
            c.f = v[i].y; d[1] = c.u;
            c.f = v[i].z; d[2] = c.u;
            c.f = v[i].rhw; d[3] = c.u;
            d[4] = v[i].bgra;
            d[5] = v[i].spec;
            c.f = v[i].u0; d[6] = c.u;
            c.f = v[i].v0; d[7] = c.u;
            if (dw == 10) {
                c.f = v[i].u1; d[8] = c.u;
                c.f = v[i].v1; d[9] = c.u;
            }
            dused += (uint32_t)dw * 4u;
        }
        mga_dma_pending = 1;            /* register writes wait for these */
        mga_fifo_free = 0;
        warp_tris += (uint32_t)(n / 3);
        return 0;
    }
    dma_mode(1);
    for (i = 0; i < n; i++) {
        union { float f; uint32_t u; } c;
        uint32_t d[10];
        c.f = v[i].x; d[0] = c.u;
        c.f = v[i].y; d[1] = c.u;
        c.f = v[i].z; d[2] = c.u;
        c.f = v[i].rhw; d[3] = c.u;
        d[4] = v[i].bgra;
        d[5] = v[i].spec;
        c.f = v[i].u0; d[6] = c.u;
        c.f = v[i].v0; d[7] = c.u;
        c.f = v[i].u1; d[8] = c.u;
        c.f = v[i].v1; d[9] = c.u;
        if (fifo_room(dw) != 0) {
            fail();
            return -1;
        }
        for (k = 0; k < dw; k++) {
            MGA_WR32(MGAREG_DMAWIN + dmawin_pos, d[k]);
            dmawin_pos = dmawin_pos + 4 < MGA_DMAWIN_SIZE ? dmawin_pos + 4 : 0;
        }
    }
    warp_tris += (uint32_t)(n / 3);
    return 0;
}
