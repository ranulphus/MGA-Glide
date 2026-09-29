/* hal.h - shared Matrox HAL (PRD D3): device discovery, capabilities,
 * mode setting, drawing-engine control and trapezoid setup. */
#ifndef MGA_HAL_H
#define MGA_HAL_H
#include "mga/types.h"

typedef enum {
    MGA_FAMILY_NONE = 0,
    MGA_FAMILY_G100,
    MGA_FAMILY_G200,
    MGA_FAMILY_G400,      /* G400 and G450 */
    MGA_FAMILY_G200E      /* server variants: reference only */
} mga_family;

typedef struct {
    /* Identity. */
    uint16_t   device_id;
    uint8_t    revision;
    uint8_t    bus, dev, fn;
    mga_family family;
    const char *name;
    uint32_t   subsys;          /* subsystem vendor:device */
    /* Apertures (physical) and PCI OPTION register. */
    uint32_t   fb_phys, fb_size;
    uint32_t   mmio_phys;
    uint32_t   iload_phys;
    uint32_t   option;
    uint32_t   vram_bytes;
    int        emulated;        /* 86Box unit tester present */
    /* Capabilities (PRD §5.3; plan §3). */
    uint8_t    fifo_depth, fifo_mask;
    uint8_t    max_mip_levels;  /* 1 (G100), 5 (G200), 11 (G400) */
    uint8_t    min_tex_dim;
    uint16_t   max_tex_size;
    unsigned   has_dwgsync:1, has_dstorg:1, has_ydstorg:1, zorg_ydst_relative:1,
               has_texctl2:1, has_alpha_blend:1, has_alpha_test:1, has_tlut:1,
               has_specular:1, has_decalblend:1, fog_textrap_only:1,
               alphasel_tex_always:1, blk_clear_ok:1, g400_clip_quirk:1,
               has_dual_tex:1;  /* G400: two texture maps, TDUALSTAGE combiner */
} mga_chip;

extern mga_chip mga;                 /* the device in use */
extern volatile uint8_t *mga_fb;     /* mapped framebuffer (BAR0) */

/* pci.c / chip.c */
uint32_t mga_pci_read32(int bus, int dev, int fn, int reg);
void     mga_pci_write32(int bus, int dev, int fn, int reg, uint32_t v);
int      mga_find(mga_chip *c);      /* 0 on success */
int      mga_map(mga_chip *c);       /* maps MMIO + framebuffer */
void     mga_unmap(void);
const char *mga_family_name(mga_family f);
int      mga_detect_emulator(void);
void     mga_chip_caps(mga_chip *c);   /* fill capabilities from c->family */
uint32_t mga_probe_vram(void);

/* vbe.c: modes via the card BIOS (VBE 2.0+). */
typedef struct {
    uint16_t mode, width, height, bpp, pitch_bytes;
    uint32_t lfb_phys;
    uint8_t  red_size, red_pos, green_size, green_pos, blue_size, blue_pos;
} mga_vbe_mode;
int  vbe_find_mode(int w, int h, int bpp, mga_vbe_mode *out);
int  vbe_enumerate(void (*cb)(const mga_vbe_mode *m, void *ctx), void *ctx);
int  vbe_set_mode(const mga_vbe_mode *m, int pitch_px, int *actual_pitch_px);
int  vbe_set_display_start(uint32_t byte_offset, int pitch_bytes, int bpp);
void vbe_set_text_mode(void);
int  vbe_version(void);
uint32_t vbe_total_memory(void);   /* bytes, from the VBE info block (0 if unknown) */

/* The mode planner: how a requested size is shown with the modes this
 * card's BIOS offers (16 bpp means RGB565; only modes on the card's own
 * aperture). In order of preference:
 *   NATIVE   the BIOS has the size: draw straight into the display buffers;
 *   ZOOM     (MGA_PLAN_ZOOM) the BIOS has twice the size: draw into the
 *            display buffers at the BIOS pitch and turn on the chip's line
 *            and pixel doubling (vbe_set_zoom), same monitor signal;
 *   INTEGER  2x, 3x or 4x the size is a BIOS mode: draw into a render
 *            buffer and scale it up at every swap (engine_present);
 *   FILL     a CRT-era size (320x200, 640x200, 640x350, 640x400, 400x256,
 *            512x256): stretched to fill the smallest 4:3 mode, as a CRT
 *            showed it;
 *   ASPECT   anything else: the smallest mode that holds it, scaled
 *            evenly, centred with black bars;
 *   TOPLEFT  (MGA_PLAN_TOPLEFT, instead of scaling) the smallest mode that
 *            holds it, drawn 1:1 in its top-left corner.
 * MGA_PLAN_FORCE takes the scaled path (INTEGER at 1x) even when the BIOS
 * has the size: a test switch. dx, dy, dw, dh is where the picture lands
 * in the display mode. */
enum { MGA_FIT_NATIVE, MGA_FIT_ZOOM, MGA_FIT_INTEGER, MGA_FIT_FILL, MGA_FIT_ASPECT, MGA_FIT_TOPLEFT };
#define MGA_PLAN_ZOOM    1u
#define MGA_PLAN_TOPLEFT 2u
#define MGA_PLAN_FORCE   4u
typedef struct {
    int          w, h;             /* the requested size: what the program draws */
    mga_vbe_mode disp;             /* the BIOS mode shown */
    int          fit;              /* MGA_FIT_* */
    int          zoom;             /* 2 for MGA_FIT_ZOOM, else 1 */
    int          dx, dy, dw, dh;   /* the picture's rectangle in the display mode */
} mga_mode_plan;
int  mga_plan_from_list(const mga_vbe_mode *list, int n, int w, int h, int bpp, unsigned flags, mga_mode_plan *p);
int  vbe_plan_mode(int w, int h, int bpp, unsigned flags, mga_mode_plan *p);   /* 0, or -1: nothing fits */
const char *mga_fit_name(int fit);
int  mga_pow2_pitch(int w);        /* a render surface's pitch: the next power of two, at least 32 */
/* Line and pixel doubling for MGA_FIT_ZOOM: CRTC9's maxscan repeats lines
 * and XZOOMCTRL repeats pixels (factor 1, 2 or 4), after vbe_set_mode.
 * Undone by vbe_set_zoom(1) and before text mode. */
void vbe_set_zoom(int factor);

/* engine.c */
typedef struct {
    uint32_t color_off;    /* byte offset of the colour buffer in VRAM */
    uint32_t z_off;        /* byte offset of the depth buffer (0 = none) */
    int      pitch_px;     /* shared by colour and depth */
    int      bpp;          /* 16 or 32 */
    int      zbits;        /* 16 or 32 */
} mga_target;

void     engine_init(int pitch_px, int bpp);
int      engine_sync(uint32_t timeout_us);     /* 0 ok, -1 timeout (engine reset) */
void     engine_reset(void);
void     engine_set_target(const mga_target *t);
void     engine_set_maccess_flags(uint32_t flags);     /* MACCESS_NODITHER / MACCESS_FOGEN */
void     engine_set_clip(int x0, int y0, int x1, int y1);   /* inclusive-exclusive */
void     engine_fill(int x, int y, int w, int h, uint32_t value);
void     engine_fill_depth(int x, int y, int w, int h, uint32_t zvalue);
void     engine_tlut_load(uint32_t off, int first, int count);   /* G200+: texture LUT from VRAM (RGB565) */
/* G200+: write a w x h rectangle at (x, y) of the surface at VRAM offset off
 * (pitch_px pixels a line, bpp 8 or 16) through the engine (ILOAD), queued in
 * order with drawing. begin returns the dwords per row (pixels in the
 * surface's format, first pixel in the low bits, each row padded to a
 * dword), or 0 when the surface can't be loaded so (pitch not a multiple of
 * 32 pixels, or 64 at 8 bpp; origin not 64-byte aligned); then exactly h
 * calls of engine_iload_row, then engine_iload_end. */
int      engine_iload_begin(uint32_t off, int pitch_px, int bpp, int x, int y, int w, int h);
void     engine_iload_row(const uint32_t *d);
void     engine_iload_end(void);
int      engine_vsync_wait(uint32_t timeout_us);
int      engine_in_vblank(void);
uint32_t engine_vcount(void);
extern const mga_target *engine_target;
extern uint32_t engine_resets, engine_timeouts;
/* The engine state the HAL owns (target, clip, MACCESS flags), saved and
 * restored around work that borrows the engine, such as engine_present. */
typedef struct { mga_target t; int clip[4]; uint32_t maccess_flags; } mga_engine_state;
void     engine_save(mga_engine_state *s);
void     engine_restore(const mga_engine_state *s);

/* present.c: draw a 16-bit render surface onto a display surface, scaled
 * to the rectangle (dx, dy, dw, dh), for the scaled fits of the mode
 * planner. The render surface is sampled as one TW16 texture, so its pitch
 * must be a power of two, a multiple of 32 and at most 2048 texels, its
 * height at most 2048 and its offset 64-byte aligned (mga_present_ok).
 * Nearest filtering gives exact pixel repetition at 2x (texel = floor of
 * (pixel centre x source/destination + 1/64)); bilinear maps the
 * first and last pixel centres to the first and last texel centres. The
 * caller has finished drawing the render surface (engine drained) and runs
 * inside its FPU guard. Overwritten and not restored, so the caller must
 * re-emit them before its next draw: DWGCTL, TEXORG, TEXCTL, TEXCTL2,
 * TEXFILTER, TEXTRANS, TEXWIDTH, TEXHEIGHT, TMR0-8, the DR registers,
 * ALPHACTRL, PLNWT and, on the G400, TDUALSTAGE0/1. Target, clip and MACCESS
 * flags are restored; setup_stats is left as it was. */
typedef struct { uint32_t off; int w, h, pitch_px; } mga_surface;
enum { MGA_PRESENT_NEAREST, MGA_PRESENT_BILINEAR };
int      mga_present_ok(const mga_surface *src);
void     engine_present(const mga_surface *src, const mga_surface *dst, int dx, int dy, int dw, int dh,
                        int filter);

/* dac.c */
void dac_set_ramp(const uint8_t ramp[256]);

/* fifo.c: the single choke point for register writes (PRD §10). */
void fifo_reserve(int n);
void fifo_reset(void);

#endif
