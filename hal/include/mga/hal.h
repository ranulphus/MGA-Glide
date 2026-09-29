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
int      engine_vsync_wait(uint32_t timeout_us);
int      engine_in_vblank(void);
uint32_t engine_vcount(void);
extern const mga_target *engine_target;
extern uint32_t engine_resets, engine_timeouts;

/* dac.c */
void dac_set_ramp(const uint8_t ramp[256]);

/* fifo.c: the single choke point for register writes (PRD §10). */
void fifo_reserve(int n);
void fifo_reset(void);

#endif
