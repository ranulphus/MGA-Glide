/* engine.c - drawing-engine initialisation, synchronisation, render
 * targets, clipping and solid fills. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"

static mga_target cur;
uint32_t engine_resets, engine_timeouts;
const mga_target *engine_target = &cur;
static uint32_t cur_maccess;
static int cur_ydstorg_px;
static int clip_x0, clip_y0, clip_x1 = 0x7FFF, clip_y1 = 0x7FFF;

static void emit_clip(void)
{
    uint32_t base = (uint32_t)cur_ydstorg_px;
    int x0 = clip_x0, y0 = clip_y0, x1 = clip_x1, y1 = clip_y1;
    if (x1 <= x0 || y1 <= y0) {
        x0 = 0; x1 = 0; y0 = 1; y1 = 0;      /* empty: nothing passes */
    }
    fifo_reserve(3);
    MGA_WR32(MGAREG_CXBNDRY, ((uint32_t)(x1 - 1) << 16) | (uint32_t)x0);
    MGA_WR32(MGAREG_YTOP, (uint32_t)y0 * (uint32_t)cur.pitch_px + base);
    MGA_WR32(MGAREG_YBOT, (uint32_t)(y1 - 1) * (uint32_t)cur.pitch_px + base);
}

static uint32_t maccess_for(int bpp, int zbits)
{
    uint32_t m = (bpp == 32) ? MACCESS_PW32 : MACCESS_PW16;
    if (zbits == 32)
        m |= MACCESS_ZW32;
    return m;
}

void engine_init(int pitch_px, int bpp)
{
    engine_sync(200000);
    fifo_reset();
    fifo_reserve(10);
    MGA_WR32(MGAREG_PITCH, (uint32_t)pitch_px);
    if (mga.has_ydstorg)
        MGA_WR32(MGAREG_YDSTORG, 0);
    cur_maccess = maccess_for(bpp, 16);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
    MGA_WR32(MGAREG_PLNWT, 0xFFFFFFFFu);
    MGA_WR32(MGAREG_FCOL, 0);
    MGA_WR32(MGAREG_BCOL, 0);
    MGA_WR32(MGAREG_CXBNDRY, 0xFFFF0000u);
    MGA_WR32(MGAREG_YTOP, 0);
    MGA_WR32(MGAREG_YBOT, 0x007FFFFFu);
    if (mga.has_dstorg) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_SRCORG, 0);
        MGA_WR32(MGAREG_DSTORG, 0);
    }
    cur.pitch_px = pitch_px;
    cur.bpp = bpp;
    cur.zbits = 16;
    cur.color_off = cur.z_off = 0;
    cur_ydstorg_px = 0;
}

int engine_sync(uint32_t timeout_us)
{
    if (timeout_us < 120000u)
        timeout_us = 120000u;          /* at least two BIOS ticks */
    uint32_t start = sys_time_us(), polls = 0;
    /* Idle means the bus FIFO is empty (FIFOSTATUS.bempty, bit 9) and the
     * drawing engine is not busy; commands can still be queued while the
     * engine reports idle. */
    while (!(MGA_RD32(MGAREG_FIFOSTATUS) & (1u << 9)) || (MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS)) {
        if ((++polls & 1023) == 0 && (uint32_t)(sys_time_us() - start) > timeout_us) {
            engine_timeouts++;
            engine_reset();
            return -1;
        }
    }
    /* Flush the read cache before the CPU touches VRAM (writing the CRTC
     * index register is the documented way). */
    MGA_WR8(MGAREG_CRTC_INDEX, 0);
    fifo_reset();
    return 0;
}

void engine_reset(void)
{
    engine_resets++;
    MGA_WR32(MGAREG_RST, 1);
    sys_delay_us(20);
    MGA_WR32(MGAREG_RST, 0);
    fifo_reset();
    engine_init(cur.pitch_px ? cur.pitch_px : 1024, cur.bpp ? cur.bpp : 16);
}

static void write_origin(uint32_t color_off, int bpp, uint32_t z_off, int zbits)
{
    uint32_t bpp_bytes = (uint32_t)bpp / 8, zbytes = (uint32_t)zbits / 8;
    if (mga.has_dstorg) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_DSTORG, color_off);
        MGA_WR32(MGAREG_ZORG, z_off);
        cur_ydstorg_px = 0;
    } else {
        /* G100: the origin is YDSTORG in pixels; the Z address is computed
         * from the same linear pixel index, so ZORG compensates. */
        cur_ydstorg_px = (int)(color_off / bpp_bytes);
        fifo_reserve(2);
        MGA_WR32(MGAREG_YDSTORG, (uint32_t)cur_ydstorg_px);
        if (mga.zorg_ydst_relative)
            MGA_WR32(MGAREG_ZORG, z_off - (uint32_t)cur_ydstorg_px * zbytes);
        else
            MGA_WR32(MGAREG_ZORG, z_off);
        emit_clip();        /* YTOP/YBOT are linear addresses including YDSTORG */
    }
}

void engine_set_target(const mga_target *t)
{
    cur = *t;
    cur_maccess = maccess_for(t->bpp, t->zbits);
    write_origin(t->color_off, t->bpp, t->z_off, t->zbits);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
}

void engine_set_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1;
    emit_clip();
}

static void solid_fill(int x, int y, int w, int h, uint32_t fcol)
{
    if (w <= 0 || h <= 0)
        return;
    fifo_reserve(4);
    MGA_WR32(MGAREG_DWGCTL, DWG_OPCOD_TRAP | DWG_ATYPE_RSTR | DWG_SOLID | DWG_ARZERO |
                            DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY | DWG_TRANSC |
                            DWG_BLTMOD_BMONOLEF);
    MGA_WR32(MGAREG_FCOL, fcol);
    MGA_WR32(MGAREG_FXBNDRY, ((uint32_t)(x + w) << 16) | (uint32_t)x);
    MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)y << 16) | (uint32_t)h);
}

void engine_fill(int x, int y, int w, int h, uint32_t value)
{
    uint32_t fcol = value;
    if (cur.bpp == 16)
        fcol = (value & 0xFFFF) | (value << 16);
    solid_fill(x, y, w, h, fcol);
}

void engine_fill_depth(int x, int y, int w, int h, uint32_t zvalue)
{
    int zb = cur.zbits;
    uint32_t fcol = zb == 16 ? ((zvalue & 0xFFFF) | (zvalue << 16)) : zvalue;
    if (!cur.z_off && !mga.has_dstorg)
        return;
    /* Retarget the colour path at the depth buffer, fill, and restore. */
    write_origin(cur.z_off, zb, cur.z_off, zb);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, (zb == 32 ? MACCESS_PW32 : MACCESS_PW16) | MACCESS_NODITHER);
    solid_fill(x, y, w, h, fcol);
    write_origin(cur.color_off, cur.bpp, cur.z_off, cur.zbits);
    fifo_reserve(1);
    MGA_WR32(MGAREG_MACCESS, cur_maccess);
}

int engine_in_vblank(void)
{
    return (MGA_RD8(MGAREG_INSTS1) & 0x08) != 0;
}

uint32_t engine_vcount(void)
{
    return MGA_RD32(MGAREG_VCOUNT) & 0xFFF;
}

int engine_vsync_wait(uint32_t timeout_us)
{
    if (timeout_us < 120000u)
        timeout_us = 120000u;
    uint32_t start = sys_time_us();
    while (engine_in_vblank())
        if ((uint32_t)(sys_time_us() - start) > timeout_us)
            return -1;
    while (!engine_in_vblank())
        if ((uint32_t)(sys_time_us() - start) > timeout_us)
            return -1;
    return 0;
}
