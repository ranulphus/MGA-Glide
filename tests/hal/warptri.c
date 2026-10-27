/* warptri.c - WARPTRI: the G400's WARP setup engine driven by PIO, with no
 * bus mastering (speed plan W0; docs/g400-warp.md). The microcode is one
 * pipe of linux-firmware's matrox/g400_warp.fw (MIT, Copyright 1999 Matrox
 * Graphics Inc.), read from the file given:
 *
 *   WARPTRI G400WARP.FW [pipe=N] [n=COUNT]
 *
 * a) the pipe (default 0, TGZ) loaded into both WARPs' instruction memories
 *    (WMISC caching off, WIMEMADDR and WIMEMDATA) and read back;
 * b) a Gouraud triangle through the WARP (OPMODE vertex write into the DMA
 *    window; Mesa's mga vertex: x, y, z, 1/w, BGRA, specular and fog, u, v,
 *    with Mesa's subpixel offset -0.5, -0.375) on the left, the same through
 *    setup_triangle 320 pixels to the right, compared pixel by pixel;
 * b2) two triangles in one batch, no depth (the G400's two WARPs take
 *    alternate triangles): both drawn?
 * c) two triangles crossing in depth (ZI, less-than), both ways;
 * d) COUNT small triangles each way (default 4000), timed with RDTSC
 *    including the wait for the engine: cycles a triangle.
 *
 * HX-TEST lines: ucode, warp-draws, gouraud, depth; HX-STAT lines give the
 * counts and timings. The picture is W0.PPM. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480
#define PITCH 1024
#define ZOFF (PITCH * SH * 2)

static uint8_t fw[64 * 1024];
static const uint8_t *pipe_code[32];
static int pipe_len[32], npipes;
static uint32_t dmawin_pos;
static int aborted;             /* the FIFO stopped draining: no more writes (they would stall the bus) */

/* Room for n writes in the bus FIFO, waiting at most about 100 ms. Unlike
 * fifo_reserve this never writes into a full FIFO: a WARP that stops taking
 * vertices would otherwise hang the PCI bus. */
static int fifo_wait(int n)
{
    long spin;
    if (aborted)
        return -1;
    for (spin = 0; spin < 200000; spin++)
        if ((int)(MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask) >= n)
            return 0;
    aborted = 1;
    hx_log("HX-STAT warptri abort: the FIFO stopped draining status=%08lx", (unsigned long)MGA_RD32(MGAREG_STATUS));
    return -1;
}

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Linux's binary ihex: records of a big-endian address and length, then the
 * data, each padded to 4 bytes; a zero length ends the file. One record a
 * pipe, in the order of the pipe numbers (MGA_WARP_TGZ = 0 ... T2GZSAF = 15). */
static int load_fw(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n, off = 0;
    if (!f)
        return -1;
    n = fread(fw, 1, sizeof fw, f);
    fclose(f);
    while (off + 6 <= n && npipes < 32) {
        unsigned len = ((unsigned)fw[off + 4] << 8) | fw[off + 5];
        if (!len || off + 6 + len > n)
            break;
        pipe_code[npipes] = fw + off + 6;
        pipe_len[npipes++] = (int)len;
        off = (off + 6 + len + 3) & ~(size_t)3;
    }
    return npipes ? 0 : -1;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The pipe into one WARP's instruction memory from word 0: two dwords a word,
 * the low one first. Returns the words that read back different. */
static int ucode_load(uint32_t data_reg, const uint8_t *code, int len)
{
    int words = (len + 7) / 8, i, bad = 0;
    MGA_WR32(MGAREG_WIMEMADDR, 0);
    for (i = 0; i < words; i++) {
        MGA_WR32(data_reg, le32(code + 8 * i));
        MGA_WR32(data_reg + 4, le32(code + 8 * i + 4));
    }
    for (i = 0; i < words; i++) {
        uint32_t lo, hi;
        MGA_WR32(MGAREG_WIMEMADDR, (uint32_t)i);
        lo = MGA_RD32(data_reg);
        hi = MGA_RD32(data_reg + 4);
        if (lo != le32(code + 8 * i) || hi != le32(code + 8 * i + 4)) {
            if (bad < 3)
                hx_log("HX-STAT warptri ucode word=%d got=%08lx:%08lx want=%08lx:%08lx", i, (unsigned long)hi,
                       (unsigned long)lo, (unsigned long)le32(code + 8 * i + 4), (unsigned long)le32(code + 8 * i));
            bad++;
        }
    }
    return bad;
}

/* The DRM's mga_warp_init and mga_g400_emit_pipe (single texture), then the
 * pipe started from instruction word 0. */
static void warp_start(void)
{
    fifo_reserve(16);
    MGA_WR32(MGAREG_WGETMSB, 0x00000E00);
    MGA_WR32(MGAREG_WVRTXSZ, 0x00001807);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0);
    MGA_WR32(MGAREG_WACCEPTSEQ, 0x18000000);
    MGA_WR32(MGAREG_WFLAG, 0);
    MGA_WR32(MGAREG_WFLAG1, 0);
    MGA_WR32(MGAREG_WR(56), 0x46480000);        /* 12800.0f */
    MGA_WR32(MGAREG_WR(49), 0);
    MGA_WR32(MGAREG_WR(57), 0);
    MGA_WR32(MGAREG_WR(53), 0);
    MGA_WR32(MGAREG_WR(61), 0);
    MGA_WR32(MGAREG_WR(54), 0x40);              /* texture sizes | MGA_G400_WR_MAGIC */
    MGA_WR32(MGAREG_WR(62), 0x40);
    MGA_WR32(MGAREG_WR(52), 0x40);
    fifo_reserve(2);
    MGA_WR32(MGAREG_WR(60), 0x40);
    MGA_WR32(MGAREG_WIADDR2, 0 | WMODE_START);
}

typedef struct { float x, y, z; uint8_t r, g, b; } vtx;

/* One vertex through the DMA window in vertex mode: 8 dwords. */
static void warp_vertex(const vtx *v, float ox, float oy)
{
    union { float f; uint32_t u; } c[4];
    uint32_t d[8];
    int i;
    c[0].f = v->x + ox; c[1].f = v->y + oy; c[2].f = v->z * (1.0f / 65535.0f); c[3].f = 1.0f;   /* z 0..1 (Mesa's depth_scale) */
    d[0] = c[0].u; d[1] = c[1].u; d[2] = c[2].u; d[3] = c[3].u;
    d[4] = (uint32_t)v->b | ((uint32_t)v->g << 8) | ((uint32_t)v->r << 16) | 0xFF000000u;
    d[5] = 0xFF000000u;                         /* no specular; fog 255 (none) */
    d[6] = 0; d[7] = 0;                         /* u0, v0 */
    if (fifo_wait(8))
        return;
    for (i = 0; i < 8; i++) {
        MGA_WR32(MGAREG_DMAWIN + dmawin_pos, d[i]);
        dmawin_pos = dmawin_pos + 4 < MGA_DMAWIN_SIZE ? dmawin_pos + 4 : 0;
    }
}

static void cpu_tri(const vtx *t, int dx, uint32_t dwgctl, uint32_t flags)
{
    mga_svtx s[3];
    mga_tri_ctx ctx;
    int i;
    memset(s, 0, sizeof s);
    memset(&ctx, 0, sizeof ctx);
    for (i = 0; i < 3; i++) {
        s[i].X16 = (int32_t)((t[i].x + dx) * 16.0f + 0.5f);
        s[i].Y16 = (int32_t)(t[i].y * 16.0f + 0.5f);
        s[i].z = t[i].z;
        s[i].r = t[i].r; s[i].g = t[i].g; s[i].b = t[i].b; s[i].a = 255;
        s[i].fog = 255;
    }
    ctx.dwgctl = dwgctl;
    ctx.flags = flags;
    ctx.clip_y0 = 0;
    ctx.clip_y1 = SH;
    setup_triangle(&s[0], &s[1], &s[2], &ctx);
}

static uint32_t opmode_saved;

static void vertex_mode(int on)
{
    engine_sync(200000);                        /* OPMODE is not queued behind drawing */
    if (on) {
        opmode_saved = MGA_RD32(MGAREG_OPMODE);
        MGA_WR32(MGAREG_OPMODE, (opmode_saved & ~OPMODE_DMAMOD_MASK) | OPMODE_DMAMOD_VERTEX);
        dmawin_pos = 0;
    } else
        MGA_WR32(MGAREG_OPMODE, opmode_saved);  /* (writing byte 0 also ends the vertex sequence) */
}

static void context(uint32_t dwgctl)
{
    fifo_reserve(8);
    MGA_WR32(MGAREG_DWGCTL, dwgctl);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_ALPHACTRL, ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
    MGA_WR32(MGAREG_FOGCOL, 0);
    MGA_WR32(MGAREG_FCOL, 0);
    MGA_WR32(MGAREG_TDUALSTAGE0, 0);
    MGA_WR32(MGAREG_TDUALSTAGE1, 0);
    MGA_WR32(MGAREG_WFLAG, 0);
    setup_invalidate();                         /* (setup_triangle's shadows: the WARP wrote the registers) */
}

static uint16_t px(int x, int y) { return ((volatile uint16_t *)mga_fb)[y * PITCH + x]; }

/* Pixels of the box (x0, y0)-(x1, y1) against the same box dx to the right:
 * coverage differences and colour differences beyond tol (per channel, 8-bit). */
static void compare(int x0, int y0, int x1, int y1, int dx, int tol, int *cover, int *colour, int *drawn)
{
    int x, y;
    *cover = *colour = *drawn = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            uint16_t a = px(x, y), b = px(x + dx, y);
            if (a)
                (*drawn)++;
            if (!a != !b)
                (*cover)++;
            else if (a && b) {
                int dr = abs((a >> 11) - (b >> 11)) << 3, dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) << 2,
                    db = abs((a & 31) - (b & 31)) << 3;
                if (dr > tol || dg > tol || db > tol)
                    (*colour)++;
            }
        }
}

static void save(void)
{
    static uint8_t rgb[SW * SH * 3];
    int x, y;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            uint16_t p = px(x, y);
            uint8_t *o = rgb + 3 * (y * SW + x);
            o[0] = (uint8_t)((p >> 11) << 3);
            o[1] = (uint8_t)(((p >> 5) & 63) << 2);
            o[2] = (uint8_t)((p & 31) << 3);
        }
    hx_save_ppm("W0", SW, SH, rgb);
}

int main(int argc, char **argv)
{
    static const vtx g[3] = { { 40, 30, 0, 255, 0, 0 }, { 290, 60, 0, 0, 255, 0 }, { 120, 200, 0, 0, 0, 255 } };
    static const vtx z1[3] = { { 30, 260, 1000, 255, 255, 0 }, { 280, 270, 60000, 255, 255, 0 }, { 150, 460, 30000, 255, 255, 0 } };
    static const vtx z2[3] = { { 40, 450, 60000, 0, 255, 255 }, { 290, 300, 1000, 0, 255, 255 }, { 200, 470, 30000, 0, 255, 255 } };
    const char *fwpath = NULL;
    int pipe = 0, count = 4000, i, ok, pitch, bad0, bad1, cover, colour, drawn;
    mga_vbe_mode mode;
    mga_target tg;
    uint64_t t0, t1;
    uint32_t dw_i = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_SHFTZERO | DWG_BOP_COPY;
    uint32_t dw_z = DWG_OPCOD_TRAP | DWG_ATYPE_ZI | DWG_ZMODE_ZLT | DWG_SHFTZERO | DWG_BOP_COPY;

    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "pipe=", 5)) pipe = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "n=", 2)) count = atoi(argv[i] + 2);
        else if (!fwpath) fwpath = argv[i];
    }
    hx_init(1, argv, "warptri");
    if (!fwpath || load_fw(fwpath) != 0 || pipe < 0 || pipe >= npipes) {
        hx_test("init", 0, "no microcode (WARPTRI G400WARP.FW [pipe=N]): %s", fwpath ? fwpath : "-");
        hx_done(HX_INIT_FAILED);
    }
    ok = mga_find(&mga) == 0 && mga_map(&mga) == 0;
    if (!ok || mga.family != MGA_FAMILY_G400) {
        hx_test("init", 0, "a G400 or G450 is needed");
        hx_done(HX_INIT_FAILED);
    }
    if (vbe_find_mode(SW, SH, 16, &mode) != 0 || vbe_set_mode(&mode, PITCH, &pitch) != 0 || pitch != PITCH) {
        hx_test("init", 0, "640x480x16 with pitch %d", PITCH);
        hx_done(HX_INIT_FAILED);
    }
    engine_init(pitch, 16);
    memset(&tg, 0, sizeof tg);
    tg.color_off = 0; tg.z_off = ZOFF; tg.pitch_px = PITCH; tg.bpp = 16; tg.zbits = 16;
    engine_set_target(&tg);
    engine_set_clip(0, 0, SW, SH);
    engine_fill(0, 0, SW, SH, 0);
    engine_fill_depth(0, 0, SW, SH, 0xFFFF);
    engine_sync(200000);
    hx_log("HX-STAT warptri pipes=%d pipe=%d bytes=%d status=%08lx wmisc=%08lx", npipes, pipe, pipe_len[pipe],
           (unsigned long)MGA_RD32(MGAREG_STATUS), (unsigned long)MGA_RD32(MGAREG_WMISC));

    /* a) the microcode */
    fifo_reserve(1);
    MGA_WR32(MGAREG_WIADDR2, WMODE_SUSPEND);
    engine_sync(200000);
    MGA_WR32(MGAREG_WMISC, 0);
    bad0 = ucode_load(MGAREG_WIMEMDATA, pipe_code[pipe], pipe_len[pipe]);
    bad1 = ucode_load(MGAREG_WIMEMDATA1, pipe_code[pipe], pipe_len[pipe]);   /* (WARP1's reads need WIMEMADDR1) */
    hx_test("ucode", bad0 == 0, "%d words; read back different: warp0 %d (warp1 not readable here: %d); WMISC %08lx",
            (pipe_len[pipe] + 7) / 8, bad0, bad1, (unsigned long)MGA_RD32(MGAREG_WMISC));
    warp_start();
    engine_sync(200000);
    {
        long spin;
        uint32_t st = 0;
        for (spin = 0; spin < 200000 && !((st = MGA_RD32(MGAREG_STATUS)) & (STATUS_WBUSY | STATUS_WBUSY1)); spin++)
            ;
        hx_log("HX-STAT warptri started status=%08lx", (unsigned long)st);
        if (!(st & (STATUS_WBUSY | STATUS_WBUSY1))) {
            hx_test("warp-running", 0, "neither WARP busy after START (status %08lx): no vertices sent", (unsigned long)st);
            aborted = 1;
        }
    }
    if (aborted)
        goto out;

    /* b) Gouraud */
    context(dw_i);
    vertex_mode(1);
    for (i = 0; i < 3; i++)
        warp_vertex(&g[i], -0.5f, -0.375f);
    vertex_mode(0);
    engine_sync(200000);
    hx_log("HX-STAT warptri after-tri status=%08lx", (unsigned long)MGA_RD32(MGAREG_STATUS));
    context(dw_i);
    cpu_tri(g, 320, dw_i, MGA_S_COLOR);
    engine_sync(200000);
    compare(0, 0, 320, 240, 320, 24, &cover, &colour, &drawn);
    hx_test("warp-draws", drawn > 1000, "%d pixels drawn by the WARP in the top-left quarter", drawn);
    hx_test("gouraud", drawn > 1000 && cover <= drawn / 100 && colour <= drawn / 100,
            "against setup_triangle: %d coverage, %d colour differences of %d pixels (offset -0.5,-0.375)", cover,
            colour, drawn);

    if (aborted)
        goto out;

    /* b2) two triangles in one batch: WARP0 and WARP1 */
    {
        static const vtx p1[3] = { { 340, 250, 0, 255, 0, 255 }, { 460, 250, 0, 255, 0, 255 }, { 400, 330, 0, 255, 0, 255 } };
        static const vtx p2[3] = { { 480, 250, 0, 0, 255, 0 }, { 600, 250, 0, 0, 255, 0 }, { 540, 330, 0, 0, 255, 0 } };
        int d1, d2, x, y;
        context(dw_i);
        vertex_mode(1);
        for (i = 0; i < 3; i++)
            warp_vertex(&p1[i], -0.5f, -0.375f);
        for (i = 0; i < 3; i++)
            warp_vertex(&p2[i], -0.5f, -0.375f);
        vertex_mode(0);
        engine_sync(200000);
        d1 = d2 = 0;
        for (y = 250; y < 330; y++)
            for (x = 340; x < 600; x++)
                if (px(x, y))
                    *(x < 470 ? &d1 : &d2) += 1;
        hx_test("two-warps", d1 > 1000 && d2 > 1000, "first triangle %d pixels, second %d", d1, d2);
        engine_fill(330, 240, 300, 100, 0);
        engine_sync(200000);
    }

    if (aborted)
        goto out;

    /* c) depth */
    context(dw_z);
    vertex_mode(1);
    for (i = 0; i < 3; i++)
        warp_vertex(&z1[i], -0.5f, -0.375f);
    for (i = 0; i < 3; i++)
        warp_vertex(&z2[i], -0.5f, -0.375f);
    vertex_mode(0);
    context(dw_z);
    cpu_tri(z1, 320, dw_z, MGA_S_COLOR | MGA_S_Z);
    cpu_tri(z2, 320, dw_z, MGA_S_COLOR | MGA_S_Z);
    engine_sync(200000);
    compare(0, 240, 320, 480, 320, 24, &cover, &colour, &drawn);
    hx_test("depth", drawn > 1000 && cover <= drawn / 100 && colour <= drawn / 100,
            "against setup_triangle: %d coverage, %d colour differences of %d pixels", cover, colour, drawn);
    save();

    if (aborted)
        goto out;

    /* d) speed: small triangles in a grid, both ways */
    {
        vtx t[3];
        int k;
        context(dw_i);
        engine_fill(0, 0, SW, SH, 0);
        engine_sync(200000);
        t0 = rdtsc();
        vertex_mode(1);
        for (k = 0; k < count; k++) {
            float x = (float)((k * 13) % 600), y = (float)((k * 7) % 440);
            t[0].x = x; t[0].y = y; t[1].x = x + 12; t[1].y = y + 2; t[2].x = x + 3; t[2].y = y + 11;
            for (i = 0; i < 3; i++) { t[i].z = 0; t[i].r = (uint8_t)(k * 3); t[i].g = (uint8_t)(i * 80); t[i].b = 200; }
            for (i = 0; i < 3; i++)
                warp_vertex(&t[i], -0.5f, -0.375f);
        }
        vertex_mode(0);
        engine_sync(2000000);
        t1 = rdtsc();
        hx_log("HX-STAT warptri speed path=warp tris=%d cycles=%lu per_tri=%lu", count, (unsigned long)(t1 - t0),
               (unsigned long)((t1 - t0) / (uint64_t)count));
        context(dw_i);
        t0 = rdtsc();
        for (k = 0; k < count; k++) {
            float x = (float)((k * 13) % 600), y = (float)((k * 7) % 440);
            t[0].x = x; t[0].y = y; t[1].x = x + 12; t[1].y = y + 2; t[2].x = x + 3; t[2].y = y + 11;
            for (i = 0; i < 3; i++) { t[i].z = 0; t[i].r = (uint8_t)(k * 3); t[i].g = (uint8_t)(i * 80); t[i].b = 200; }
            cpu_tri(t, 0, dw_i, MGA_S_COLOR);
        }
        engine_sync(2000000);
        t1 = rdtsc();
        hx_log("HX-STAT warptri speed path=cpu tris=%d cycles=%lu per_tri=%lu", count, (unsigned long)(t1 - t0),
               (unsigned long)((t1 - t0) / (uint64_t)count));
    }
out:
    if (!aborted) {
        fifo_reserve(1);
        MGA_WR32(MGAREG_WIADDR2, WMODE_SUSPEND);
        engine_sync(200000);
    } else
        MGA_WR32(MGAREG_WIADDRNB2, WMODE_SUSPEND);     /* (bypassing the FIFO) */
    MGA_WR32(MGAREG_OPMODE, opmode_saved);
    engine_reset();                             /* the next program finds the engine and the WARPs reset */
    vbe_set_text_mode();
    hx_done(hx_failures() ? HX_FAIL : HX_PASS);
    return 0;
}
