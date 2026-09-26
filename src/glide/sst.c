/* sst.c - initialisation, hardware query and window open/close. */
#include "glide/mg.h"
#include "trace/trace.h"
#include "mga/sys.h"
#include "mga/mmio.h"
#include "tex/texmgr.h"
#include <string.h>

mg_ctx mg;
uint32_t mga_probe_vram(void);

void mg_fatal(const char *msg)
{
    mg_log(MG_LOG_ERROR, "fatal: %s", msg);
    if (mg.error_cb)
        mg.error_cb(msg, FXTRUE);
}

int mg_device_open(void)
{
    if (mga.family != MGA_FAMILY_NONE)
        return 0;
    if (mga_find(&mga) < 0) {
        mg_log(MG_LOG_ERROR, "no supported Matrox card found");
        return -1;
    }
    mga.emulated = mga_detect_emulator();
    mg_line("MGL-CARD %s id=%04x rev=%02x family=%s emulated=%d fb=%08x mmio=%08x",
            mga.name, mga.device_id, mga.revision, mga_family_name(mga.family), mga.emulated,
            mga.fb_phys, mga.mmio_phys);
    if (mga.family == MGA_FAMILY_G200E) {
        mg_log(MG_LOG_ERROR, "server G200e variants are not supported");
        return -1;
    }
    if (mga_map(&mga) < 0) {
        mg_log(MG_LOG_ERROR, "cannot map apertures");
        return -1;
    }
    return 0;
}

GR_ENTRY(void, grGlideInit, (void))
{
    GrErrorCallbackFnc_t cb = mg.error_cb;
    if (mg.initialised)
        return;
    mg_log_init();
    memset(&mg, 0, sizeof mg);
    mg.error_cb = cb;
    mg_line("MGL-INIT MGA-Glide %s", MGA_GLIDE_VERSION);
    mg_config_load();
    trace_init_hook();
    mg.gamma = 1.7f;            /* retail Voodoo Graphics default, measured by t22_d */
    strcpy(mg.version, mg_config.report_version);
    mg_state_defaults();
    if (mg_device_open() == 0)
        mg.initialised = 1;
}

GR_ENTRY(void, grGlideShutdown, (void))
{
    if (mg.open)
        grSstWinClose();
    trace_flush();
    mg_line("MGL-SHUTDOWN frames=%u", mg.frame);
    mg.initialised = 0;
}

GR_ENTRY(void, grGlideGetVersion, (char version[80]))
{
    /* May be called before grGlideInit: load the options on demand. */
    if (!mg_config.report_version[0])
        mg_config_load();
    strcpy(version, mg_config.report_version);
}

GR_ENTRY(void, grErrorSetCallback, (void (*fnc)(const char *string, FxBool fatal)))
{
    mg.error_cb = fnc;
}

static void fill_voodoo(GrVoodooConfig_t *v)
{
    v->fbRam = mg_config.report_fb_mb;
    v->fbiRev = 2;
    v->nTexelfx = mg_config.report_tmus;
    v->sliDetect = FXFALSE;
    v->tmuConfig[0].tmuRev = 1;
    v->tmuConfig[0].tmuRam = mg_config.report_tmu_mb;
    v->tmuConfig[1].tmuRev = 1;
    v->tmuConfig[1].tmuRam = mg_config.report_tmu_mb;
}

GR_ENTRY(FxBool, grSstQueryBoards, (GrHwConfiguration *hwConfig))
{
    memset(hwConfig, 0, sizeof *hwConfig);
    if (mg_device_open() < 0)
        return FXFALSE;
    hwConfig->num_sst = 1;
    return FXTRUE;
}

GR_ENTRY(FxBool, grSstQueryHardware, (GrHwConfiguration *hwConfig))
{
    memset(hwConfig, 0, sizeof *hwConfig);
    if (mg_device_open() < 0)
        return FXFALSE;
    hwConfig->num_sst = 1;
    hwConfig->SSTs[0].type = mg_config.report_voodoo2 ? GR_SSTTYPE_Voodoo2 : GR_SSTTYPE_VOODOO;
    fill_voodoo(&hwConfig->SSTs[0].sstBoard.VoodooConfig);
    return FXTRUE;
}

GR_ENTRY(void, grSstSelect, (int which_sst))
{
    mg.sst = which_sst;
}

static const struct { int res, w, h; } resolutions[] = {
    { GR_RESOLUTION_320x200, 320, 200 }, { GR_RESOLUTION_320x240, 320, 240 },
    { GR_RESOLUTION_400x256, 400, 256 }, { GR_RESOLUTION_512x384, 512, 384 },
    { GR_RESOLUTION_640x200, 640, 200 }, { GR_RESOLUTION_640x350, 640, 350 },
    { GR_RESOLUTION_640x400, 640, 400 }, { GR_RESOLUTION_640x480, 640, 480 },
    { GR_RESOLUTION_800x600, 800, 600 }, { GR_RESOLUTION_960x720, 960, 720 },
    { GR_RESOLUTION_856x480, 856, 480 }, { GR_RESOLUTION_512x256, 512, 256 },
    { GR_RESOLUTION_1024x768, 1024, 768 }, { GR_RESOLUTION_1280x1024, 1280, 1024 },
    { GR_RESOLUTION_1600x1200, 1600, 1200 }, { GR_RESOLUTION_400x300, 400, 300 },
};

/* Choose the smallest 16-bit 565 VBE mode that can hold w x h. The game
 * draws at its own size; a larger mode shows it in the top-left corner
 * (centring is a later refinement). */
static int pick_mode(int w, int h, int bpp, mga_vbe_mode *m)
{
    static const int sizes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 } };
    unsigned i;
    if (vbe_find_mode(w, h, bpp, m) == 0)
        return 0;
    for (i = 0; i < MGA_ARRAY_LEN(sizes); i++)
        if (sizes[i][0] >= w && sizes[i][1] >= h && vbe_find_mode(sizes[i][0], sizes[i][1], bpp, m) == 0)
            return 0;
    return -1;
}

GR_ENTRY(FxBool, grSstWinOpen, (FxU32 hWnd, GrScreenResolution_t screen_resolution,
                                GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format,
                                GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers))
{
    int w = 640, h = 480, pitch = 0, i;
    unsigned r;
    uint32_t off, bytes;
    MGA_UNUSED(hWnd);
    MGA_UNUSED(refresh_rate);
    if (!mg.initialised)
        grGlideInit();
    if (!mg.initialised)
        return FXFALSE;
    if (mg.open)
        grSstWinClose();
    for (r = 0; r < MGA_ARRAY_LEN(resolutions); r++)
        if ((unsigned)resolutions[r].res == screen_resolution) {
            w = resolutions[r].w;
            h = resolutions[r].h;
        }
    mg.bpp = mg_config.bpp == 32 ? 32 : 16;
    {
        /* Resolution override: the game keeps its size, the card renders
         * at res_w x res_h and everything in between is scaled. */
        int hw = w, hh = h;
        mg.scaled = 0;
        mg.sx = mg.sy = 1.0;
        if (mg_config.res_w > 0 && mg_config.res_h > 0 && (mg_config.res_w != w || mg_config.res_h != h)) {
            hw = mg_config.res_w;
            hh = mg_config.res_h;
        }
        if (pick_mode(hw, hh, mg.bpp, &mg.mode) < 0 && mg.bpp == 32) {
            mg_log(MG_LOG_WARN, "no 32-bit VBE mode for %dx%d: using 16-bit", hw, hh);
            mg.bpp = 16;
        }
        if (mg.bpp == 16 && pick_mode(hw, hh, 16, &mg.mode) < 0) {
            mg_log(MG_LOG_ERROR, "no VBE mode for %dx%d", hw, hh);
            return FXFALSE;
        }
        if (hw != w || hh != h) {
            mg.scaled = 1;
            mg.sx = (double)hw / w;
            mg.sy = (double)hh / h;
        }
    }
    if (vbe_set_mode(&mg.mode, 1024, &pitch) < 0) {
        mg_log(MG_LOG_ERROR, "VBE mode %03x failed", mg.mode.mode);
        return FXFALSE;
    }
    if (!mga.vram_bytes)
        mga.vram_bytes = mga_probe_vram();
    mg.width = w;
    mg.height = h;
    mg.pitch_px = pitch;
    mg.nbuffers = nColBuffers < 2 ? 2 : (nColBuffers > MG_MAX_BUFFERS ? MG_MAX_BUFFERS : nColBuffers);
    mg.has_aux = nAuxBuffers > 0;
    /* 32-bit depth keeps W-buffering precise; use it whenever the colour
     * buffers, a 32-bit aux buffer and a 2 MB texture heap fit. */
    {
        uint32_t screen = (uint32_t)pitch * (uint32_t)(mg.bpp / 8) * mg.mode.height;
        uint32_t aux32 = (uint32_t)pitch * 4u * mg.mode.height;
        uint32_t need32 = (screen + 0x1000) * (uint32_t)mg.nbuffers + aux32 + (2u << 20);
        if (mg_config.force_z32 >= 0)
            mg.zbits = mg_config.force_z32 ? 32 : 16;
        else
            mg.zbits = (mga.vram_bytes >= need32) ? 32 : 16;
    }
    mg.color_format = color_format;
    mg.st.origin = origin_location == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    /* Buffers are whole screens of the display mode so page flips land on
     * line boundaries; each is 4 KB aligned. */
    bytes = (uint32_t)pitch * (uint32_t)(mg.bpp / 8) * mg.mode.height;
    off = 0;
    for (i = 0; i < mg.nbuffers; i++) {
        mg.buf_off[i] = off;
        off = (off + bytes + 0xFFF) & ~0xFFFu;
    }
    if (mg.has_aux) {
        mg.aux_off = off;
        off = (off + (uint32_t)pitch * (mg.zbits / 8u) * mg.mode.height + 0xFFF) & ~0xFFFu;
    }
    mg.heap_off = off;
    mg.heap_end = mga.vram_bytes;
    if (mg.heap_off >= mg.heap_end) {
        mg_log(MG_LOG_ERROR, "not enough VRAM for %d buffers at %dx%d", mg.nbuffers, w, h);
        return FXFALSE;
    }
    tex_heap_init(mg.heap_off, mg.heap_end);
    tex_reset();
    mg.front = 0;
    mg.back = 1;
    mg.render_buffer = GR_BUFFER_BACKBUFFER;
    engine_init(pitch, mg.bpp);
    mg.open = 1;
    mg.dirty = ~0u;
    mg.st.clip_x0 = 0; mg.st.clip_y0 = 0; mg.st.clip_x1 = w; mg.st.clip_y1 = h;
    mg_validate();
    for (i = 0; i < mg.nbuffers; i++) {
        mga_target t;
        t.color_off = mg.buf_off[i]; t.z_off = mg.aux_off; t.pitch_px = pitch; t.bpp = mg.bpp; t.zbits = mg.zbits;
        engine_set_target(&t);
        engine_set_clip(0, 0, mg.mode.width, mg.mode.height);
        engine_fill(0, 0, mg.mode.width, mg.mode.height, 0);
    }
    if (mg.has_aux)
        engine_fill_depth(0, 0, mg.mode.width, mg.mode.height, 0);
    mg.dirty = ~0u;
    engine_sync(200000);
    vbe_set_display_start(mg.buf_off[mg.front], pitch * (mg.bpp / 8), mg.bpp);
    mg_gamma_apply();
    mg.fogcol_valid = 0;
    mg_hooks_install();
    mg_line("MGL-WINOPEN %dx%d mode=%03x bpp=%d scale=%dx%d pitch=%d buffers=%d aux=%d z%d vram=%u heap=%u",
            w, h, mg.mode.mode, mg.bpp, mg_hx(w), mg_hy(h), pitch, mg.nbuffers, mg.has_aux, mg.zbits, mga.vram_bytes,
            mg.heap_end - mg.heap_off);
    return FXTRUE;
}

/* A fault while the window is open: leave the screen readable (the host
 * prints its register dump next) and the engine idle, then chain. */
static void on_fault(int exc, uint32_t err, uint32_t eip)
{
    mg_line("MGL-EXC %02x err=%x eip=%x", exc, err, eip);
    trace_flush();
    if (mg.open) {
        engine_reset();
        vbe_set_text_mode();
        mg.open = 0;
    }
    sys_unhook_exit();
    sys_unhook_faults();
}

/* The game exits to DOS without grGlideShutdown. */
static void on_exit(void)
{
    trace_flush();
    if (mg.open) {
        mg_line("MGL-EXIT-HOOK video restored");
        engine_sync(200000);
        vbe_set_text_mode();
        mg.open = 0;
    }
    sys_unhook_faults();
    sys_unhook_exit();
}

void mg_hooks_install(void)
{
    if (!mg_config.hooks)
        return;
    if (sys_hook_faults(on_fault) < 0 || sys_hook_exit(on_exit) < 0)
        mg_log(MG_LOG_WARN, "fault/exit hooks unavailable");
}

GR_ENTRY(void, grSstWinClose, (void))
{
    if (!mg.open)
        return;
    engine_sync(200000);
    vbe_set_text_mode();
    mg.open = 0;
    sys_unhook_faults();
    sys_unhook_exit();
    mg_line("MGL-WINCLOSE frames=%u engine_resets=%u timeouts=%u", mg.frame, engine_resets, engine_timeouts);
}

GR_ENTRY(FxU32, grSstScreenWidth, (void)) { return (FxU32)(mg.open ? mg.width : 640); }
GR_ENTRY(FxU32, grSstScreenHeight, (void)) { return (FxU32)(mg.open ? mg.height : 480); }

GR_ENTRY(void, grSstIdle, (void))
{
    if (mg.open)
        engine_sync(200000);
}

GR_ENTRY(FxBool, grSstIsBusy, (void))
{
    return mg.open && (MGA_RD32(0x1E14) & (1u << 16)) ? FXTRUE : FXFALSE;
}

GR_ENTRY(FxU32, grSstStatus, (void))
{
    /* Voodoo status: bit 6 vertical retrace, bits 7-9 FBI/TMU/SST busy. */
    FxU32 s = 0x0FFFF03F;      /* FIFO free counts */
    if (mg.open && engine_in_vblank())
        s |= 1u << 6;
    if (grSstIsBusy())
        s |= (1u << 7) | (1u << 9);
    return s;
}

GR_ENTRY(FxBool, grSstVRetraceOn, (void))
{
    return mg.open && engine_in_vblank() ? FXTRUE : FXFALSE;
}

GR_ENTRY(FxU32, grSstVideoLine, (void))
{
    return mg.open ? engine_vcount() : 0;
}

GR_ENTRY(void, grSstOrigin, (GrOriginLocation_t origin))
{
    mg.st.origin = origin == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
}

int mg_hx(int x) { return mg.scaled ? (int)mga_floor(x * mg.sx + 0.5) : x; }
int mg_hy(int y) { return mg.scaled ? (int)mga_floor(y * mg.sy + 0.5) : y; }
